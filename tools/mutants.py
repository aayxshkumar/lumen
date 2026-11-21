#!/usr/bin/env python3
"""Plant known interpreter bugs one at a time and see whether the tests notice.

Each mutant is applied to a scratch copy of the repo (the working tree is
never touched) and built in its own build dir; then the unit tests and the
program suite run against it.

  python3 tools/mutants.py              all mutants
  python3 tools/mutants.py div-floors   one mutant
  python3 tools/mutants.py --list

Exit code 0 if every mutant was caught, 1 otherwise.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MUTANTS = {
    "closure-copies-env": (
        "src/interpreter.cpp",
        "      v.v = std::make_shared<Closure>(e.fn, env);\n      return v;",
        "      v.v = std::make_shared<Closure>(e.fn, std::make_shared<Env>(*env));\n      return v;",
        "closures capture a copy of the environment instead of a reference",
    ),
    "loop-shares-env": (
        "src/interpreter.cpp",
        "      for (auto& item : items) {\n"
        "        // A fresh environment per iteration, so closures capture this\n"
        "        // iteration's variable.\n"
        "        auto body_env = std::make_shared<Env>(env);\n"
        "        body_env->vars.emplace(s.name, std::move(item));",
        "      auto body_env = std::make_shared<Env>(env);\n"
        "      for (auto& item : items) {\n"
        "        body_env->vars[s.name] = std::move(item);",
        "every for-loop iteration shares one environment",
    ),
    "block-no-scope": (
        "src/interpreter.cpp",
        "    case Stmt::Kind::Block:\n      return exec_block(s.body, std::make_shared<Env>(env));",
        "    case Stmt::Kind::Block:\n      return exec_block(s.body, env);",
        "a { } block doesn't open a new scope",
    ),
    "div-floors": (
        "src/interpreter.cpp",
        "        return Value::integer(x / y);  // truncates toward zero",
        "        return Value::integer((x / y) - (((x % y) != 0 && ((x < 0) != (y < 0))) ? 1 : 0));",
        "integer division rounds toward negative infinity instead of zero",
    ),
    "and-evaluates-both": (
        "src/interpreter.cpp",
        "      if (e.op == Tok::And ? !a.as_bool() : a.as_bool()) return a;\n"
        "      const Value b = eval(*e.b, env);",
        "      const Value b = eval(*e.b, env);\n"
        "      if (e.op == Tok::And ? !a.as_bool() : a.as_bool()) return a;",
        "'and'/'or' always evaluate their right side",
    ),
    "while-swallows-return": (
        "src/interpreter.cpp",
        "        if (f == Flow::Break) break;\n        if (f == Flow::Return) return f;\n      }\n      return Flow::Normal;\n    case Stmt::Kind::For:",
        "        if (f == Flow::Break) break;\n      }\n      return Flow::Normal;\n    case Stmt::Kind::For:",
        "a 'return' inside a while loop only ends the loop",
    ),
    "list-plus-mutates": (
        "src/interpreter.cpp",
        "      l->items = a.as_list().items;\n"
        "      l->items.insert(l->items.end(), b.as_list().items.begin(), b.as_list().items.end());\n"
        "      Value v;\n      v.v = l;\n      return v;",
        "      (void)l;\n"
        "      a.as_list().items.insert(a.as_list().items.end(), b.as_list().items.begin(), b.as_list().items.end());\n"
        "      return a;",
        "list + list appends to the left list instead of making a new one",
    ),
    "float-keys-distinct": (
        "src/value.cpp",
        "      if (std::isfinite(d) && d == std::trunc(d) && std::fabs(d) < 9.2e18)",
        "      if (false)",
        "map key 1.0 is a different key from 1",
    ),
    "add-unchecked": (
        "src/interpreter.cpp",
        "        if (__builtin_add_overflow(x, y, &r)) fail(e.span, \"integer overflow\");",
        "        r = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(y));",
        "integer + wraps around instead of reporting overflow",
    ),
    "stack-limit-off": (
        "src/interpreter.cpp",
        "  if (static_cast<int>(stack_.size()) >= max_depth)",
        "  if (false)",
        "no recursion limit: deep recursion crashes the process",
    ),
}


def run(cmd, cwd, timeout=600):
    try:
        return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as e:
        return subprocess.CompletedProcess(cmd, -999, e.stdout or "", "timeout")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    if args.list:
        for n, m in MUTANTS.items():
            print(f"{n:22} {m[3]}")
        return 0
    names = args.names or list(MUTANTS)
    for n in names:
        if n not in MUTANTS:
            print(f"unknown mutant {n}", file=sys.stderr)
            return 2

    ignore = shutil.ignore_patterns("build*", ".git", "__pycache__")
    survivors = []
    print(f"{'mutant':22} {'unit tests':>11} {'programs':>12}")
    with tempfile.TemporaryDirectory(prefix="lumen-mutants-") as tmp:
        for name in names:
            path, old, new, what = MUTANTS[name]
            src = os.path.join(tmp, name)
            shutil.copytree(ROOT, src, ignore=ignore)
            target = os.path.join(src, path)
            text = open(target).read()
            if text.count(old) != 1:
                print(f"{name}: anchor not found exactly once in {path}; update tools/mutants.py")
                return 2
            open(target, "w").write(text.replace(old, new))
            build = os.path.join(src, "build")
            c = run(["cmake", "-S", src, "-B", build, "-DCMAKE_BUILD_TYPE=Release"], src)
            b = run(["cmake", "--build", build, "-j", str(args.jobs)], src)
            if c.returncode or b.returncode:
                print(f"{name}: does not compile\n{b.stdout[-2500:]}{b.stderr[-1500:]}")
                return 2
            t = run([os.path.join(build, "lumen_tests"), "--gtest_brief=1"], src)
            failed_tests = sorted(set(re.findall(r"\[  FAILED  \] (\S+) \(", t.stdout)))
            unit = "pass" if t.returncode == 0 else (f"{len(failed_tests)} fail" if failed_tests else "crash")
            p = run([sys.executable, os.path.join(src, "tools", "run_tests.py"), "--lumen",
                     os.path.join(build, "lumen"), "--timeout", "20", os.path.join(src, "tests", "programs")], src)
            fails = [l[5:] for l in p.stdout.splitlines() if l.startswith("FAIL ")]
            prog = "pass" if p.returncode == 0 else f"{len(fails)} fail"
            caught = t.returncode != 0 or p.returncode != 0
            print(f"{name:22} {unit:>11} {prog:>12}   {'' if caught else 'SURVIVED  '}{what}")
            if fails:
                print(f"{'':22} programs: {', '.join(fails[:5])}{' ...' if len(fails) > 5 else ''}")
            if not caught:
                survivors.append(name)
    if survivors:
        print(f"\n{len(survivors)} mutant(s) survived: {', '.join(survivors)}")
        return 1
    print("\nall mutants caught")
    return 0


if __name__ == "__main__":
    sys.exit(main())
