# lumen

A programming language, built from scratch in C++20: a hand-written lexer and Pratt parser, a tree-walking interpreter with closures, lists and maps, precise error messages with stack traces, and a REPL. It's the start of a bigger plan (bytecode VM, garbage collector, static types, modules, a debugger and a language server; see [Roadmap](#roadmap)). Every stage is held to the same bar: a program suite that pins the language's behaviour, and planted bugs that the tests must catch.

```
$ cat hello.lm
fn make_counter() {
  let n = 0;
  return fn() { n = n + 1; return n; };
}
let c = make_counter();
c(); c();
print("count:", c());

$ ./build/lumen hello.lm
count: 3

$ ./build/lumen -e 'fn f(x) { return g(x); } fn g(y) { return y / 0; } f(1);'
<-e>:1:45: runtime error: division by zero
  fn f(x) { return g(x); } fn g(y) { return y / 0; } f(1);
                                              ^
  in g called at line 1:19
  in f called at line 1:53
```

No dependencies beyond googletest. Built on Apple clang 17 and GCC 13.

## Build and run

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/lumen file.lm              # run a file
./build/lumen                      # REPL
./build/lumen -e 'print(1 + 2);'
./build/lumen --dump-ast file.lm   # the parse tree as S-expressions
./build/lumen --stats file.lm      # call count and objects leaked by cycles

ctest --test-dir build             # unit tests + the program suite
python3 tools/run_tests.py --lumen build/lumen tests/programs -v
python3 tools/mutants.py           # plants bugs, checks the tests catch them
python3 tools/bench.py --lumen build/lumen
```

googletest 1.15.2 comes from `third_party/` when the tarball is present (offline machines), else from GitHub, pinned to one SHA-256. Sanitizers: `-DLUMEN_SANITIZERS=address,undefined`. Exit codes: 65 compile error, 70 runtime error.

## The language

```
let x = 10;                     // variables must be declared before use
let xs = [1, 2.5, "three"];     // lists
let m = {"a": 1, 2: "two"};     // insertion-ordered maps; keys: nil, bool, int, float, string
fn add(a, b) { return a + b; }  // functions; recursion and mutual recursion work
let sq = fn(v) { return v * v; };  // function values and closures

if x > 5 and not (x == 7) { print("big"); } else if x == 0 { print("zero"); } else { print("small"); }
while x > 0 { x = x - 1; }
for item in xs { print(item); }     // lists, map keys, string characters
for i in range(0, 10, 2) { if i == 6 { break; } }
```

Builtins: `print len push pop range str repr type int float keys has del join split sort abs sqrt clock assert error`.

### Rules that are deliberate

- **No truthiness.** `if`, `while`, `and`, `or` and `not` take only bools. `if len(xs) { }` is a runtime error, not a silent coercion. This is meant to carry over cleanly to a static type checker later.
- **Integer overflow is an error**, never a wraparound. Integers are 64-bit and checked on `+ - *`, negation, `/` and `abs`.
- **`/` on two ints truncates toward zero, and `%` takes the dividend's sign** (like C, unlike Python): `-7 / 2 == -3`, `-7 % 3 == -1`. Int `/ 0` is an error; float `1.0 / 0` is `inf` (IEEE).
- **`1 == 1.0` is true, and they are the same map key.** Mixed int/float arithmetic gives a float.
- **Lists and maps are references**: `let b = a; push(b, 1);` changes `a`. `a + b` on lists makes a new list.
- **`==` compares lists and maps by contents**, functions by identity.
- **Comparisons don't chain**: `a < b < c` is a compile error that suggests `and`.
- **In `if`, `while` and `for` heads, `{` starts the body.** A map literal there needs parentheses (Rust's rule).
- **Each `for` iteration gets a fresh variable**, so closures created in a loop capture that iteration's value.
- **Strings are immutable** byte strings; `s[i]` is a one-character string.
- **Deep recursion is a runtime error, not a crash**: the default limit is 10,000 nested calls, and the CLI runs the interpreter on a 1 GB stack.

## How it's built

```
source ─► lexer ─► tokens ─► Pratt parser ─► AST ─► tree-walking interpreter
                                               │
                                               └─► --dump-ast
```

- `src/lexer.cpp`: tokens with line and column; number literals with `_` separators and exponents; string escapes; nested errors point at the exact character.
- `src/parser.cpp`: recursive descent for statements and a Pratt parser for expressions (precedence: assignment < or < and < not < comparison < + - < * / % < unary - < call/index).
- `src/interpreter.cpp`: evaluates the AST directly. Environments are hash maps linked by parent pointers, and every lookup walks the chain. Control flow (`return`, `break`, `continue`) is a returned status, not exceptions.
- `src/value.cpp`: values (nil, bool, int, float, string, list, map, function, builtin), printing, deep equality, map keys.
- `src/builtins.cpp`: the standard functions.

The tree-walker is meant to stay simple and obviously correct: it's the **reference implementation** that the faster back ends on the roadmap must agree with, program for program.

## Testing

- `tests/programs/`: 47 programs that pin the language's behaviour. Each says in comments what it must print (`// expect: …`) or which error it must raise (`// error: …`), including positions and stack traces. `tools/run_tests.py` runs them; `ctest` runs them too.
- `tests/*_test.cpp`: 16 unit tests for the lexer, the parser (precedence, error positions) and the interpreter.
- `tools/mutants.py`: plants interpreter bugs one at a time in a scratch copy and runs both suites:

| Mutant | Bug | Unit tests | Programs |
|---|---|---|---|
| closure-copies-env | closures capture a copy of the environment | pass | 1 fail |
| loop-shares-env | all for iterations share one environment | 1 fail | 1 fail |
| block-no-scope | `{ }` doesn't open a scope | pass | 1 fail |
| div-floors | `/` floors instead of truncating | 1 fail | 1 fail |
| and-evaluates-both | no short-circuit | pass | 1 fail |
| while-swallows-return | `return` in a while loop only ends the loop | pass | 1 fail |
| list-plus-mutates | `a + b` appends to `a` | pass | 2 fail |
| float-keys-distinct | `m[1.0]` and `m[1]` are different keys | pass | 1 fail |
| add-unchecked | `+` wraps on overflow | 1 fail | 1 fail |
| stack-limit-off | no recursion limit (crash) | crash | 1 fail |

Every mutant is caught, but most by exactly one program. The suite pins the behaviour, but it's thin: hand-written programs only cover the cases someone thought of. A random program generator, run against two implementations, is on the roadmap.

## Memory

Values are reference counted. A function declared in a scope is stored in that scope and also captures it, so the scope and the closure keep each other alive, and neither is ever freed. `--stats` shows it:

```
$ ./build/lumen --stats tests/programs/closures.lm
...
stats: 18 calls; leaked after exit: 7 envs, 8 closures, 1 lists, 0 maps
```

That's a real leak for long-running programs. On Linux, AddressSanitizer's leak checker reports it too, so CI runs the ASan job with `ASAN_OPTIONS=detect_leaks=0` for now. A tracing garbage collector is on the roadmap, and turning leak checks back on is part of its done check.

## Performance (Apple M4, Release, tree-walker, median of 3)

| Benchmark | What it stresses | Time |
|---|---|---|
| fib | fib(30): 1.3M calls | 0.415 s |
| loop | 3M iterations of a while loop with arithmetic | 0.332 s |
| closures | 300k closure creations and calls | 0.125 s |
| nbody | 200k float-math steps | 0.127 s |
| maps | 300k string-keyed inserts and lookups | 0.117 s |
| lists | 200k pushes, a sort and indexing | 0.063 s |

Every variable access hashes a string and walks the environment chain, which is why fib and loop are slow. These numbers are the baseline the bytecode VM has to beat.

## Roadmap

The plan, roughly in order. Each step comes with tests that would catch it being wrong.

1. **Bytecode compiler and VM**: a resolver for variable slots, a stack VM with upvalues for closures, and a `--vm` flag. Every program in the suite must give byte-identical output on both back ends, plus a **random program generator** that runs thousands of generated programs through both and compares output, errors and positions. Then speedups measured on `bench/`.
2. **Garbage collector**: a tracing mark-sweep collector replacing reference counting, with a stress mode that collects on every allocation, and `--stats` reporting 0 leaked objects.
3. **Static types**: optional type annotations, local type inference, and generics for list and map, with errors that point at both sides of a mismatch. Existing untyped programs keep working.
4. **Structs, methods and interfaces.**
5. **Modules and a package manager**: `import`, a module cache, a lock file, and a local package registry.
6. **A real standard library**: strings, math, files, JSON, time.
7. **Debugger**: breakpoints, stepping, stack inspection and watch expressions, over the VM.
8. **Language server (LSP)**: diagnostics, go-to-definition, completion and hover, plus a small browser-based editor that uses it.
9. **Native code generation**: x86-64 output for a typed subset, checked against the VM.
10. **Concurrency**: green threads and channels.

## Layout

```
include/lumen/   lexer, ast, value, interpreter
src/             lexer, parser, value, interpreter, builtins
tools/           lumen.cpp (CLI + REPL), run_tests.py, mutants.py, bench.py
tests/           unit tests; programs/ holds the language suite
bench/           benchmark programs
```

MIT licence.
