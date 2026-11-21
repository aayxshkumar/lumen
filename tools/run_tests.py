#!/usr/bin/env python3
"""Run the lumen program suite.

Each tests/programs/*.lm file says what it should do in comments:

  // expect: TEXT    the next line of stdout must be exactly TEXT
  // error: TEXT     stderr must contain TEXT, and the run must fail
                     (exit 65 for a compile error, 70 for a runtime error)

Output printed before an error still has to match the expect lines. A
file passes only if stdout, the error substrings and the exit status all
match. Programs run from their own directory, so error messages show the
bare file name.

  python3 tools/run_tests.py --lumen build/lumen tests/programs
  python3 tools/run_tests.py --lumen build/lumen tests/programs/closures.lm -v

Exit code 0 if every program passed, 1 otherwise.
"""
import argparse
import os
import re
import subprocess
import sys

EXPECT = re.compile(r"// expect: ?(.*)$")
ERROR = re.compile(r"// error: ?(.*)$")


def expectations(path):
    out, errors = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            m = EXPECT.search(line)
            if m:
                out.append(m.group(1))
            m = ERROR.search(line)
            if m:
                errors.append(m.group(1).strip())
    return out, errors


def check(lumen, path, timeout):
    want_out, want_errors = expectations(path)
    d, name = os.path.split(os.path.abspath(path))
    try:
        r = subprocess.run([lumen, name], cwd=d, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return [f"timed out after {timeout} s"]
    problems = []
    got_out = r.stdout.split("\n")
    if got_out and got_out[-1] == "":
        got_out.pop()
    if got_out != want_out:
        for i in range(max(len(got_out), len(want_out))):
            g = got_out[i] if i < len(got_out) else "<missing>"
            w = want_out[i] if i < len(want_out) else "<nothing more expected>"
            if g != w:
                problems.append(f"stdout line {i + 1}: expected {w!r}, got {g!r}")
                break
    if want_errors:
        if r.returncode not in (65, 70):
            problems.append(f"expected an error, got exit code {r.returncode}")
        for e in want_errors:
            if e not in r.stderr:
                problems.append(f"stderr is missing {e!r}")
    elif r.returncode != 0:
        problems.append(f"exit code {r.returncode}, stderr: {r.stderr.strip()[:300]}")
    if r.returncode < 0:
        problems.append(f"killed by signal {-r.returncode}")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lumen", required=True)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    files = []
    for p in args.paths:
        if os.path.isdir(p):
            files += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith(".lm"))
        else:
            files.append(p)
    lumen = os.path.abspath(args.lumen)
    failed = 0
    for f in files:
        problems = check(lumen, f, args.timeout)
        if problems:
            failed += 1
            print(f"FAIL {os.path.basename(f)}")
            for p in problems:
                print(f"     {p}")
        elif args.verbose:
            print(f"ok   {os.path.basename(f)}")
    print(f"{len(files) - failed}/{len(files)} programs passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
