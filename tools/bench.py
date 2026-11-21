#!/usr/bin/env python3
"""Time every bench/*.lm program and print a table.

  python3 tools/bench.py --lumen build/lumen            median of 3 runs
  python3 tools/bench.py --lumen build/lumen --runs 5 fib

Each program prints a checksum; the table shows it so a faster back end can
be checked against the same output. Times are wall-clock medians.
"""
import argparse
import os
import statistics
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lumen", required=True)
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("names", nargs="*")
    args = ap.parse_args()
    bench_dir = os.path.join(ROOT, "bench")
    names = args.names or sorted(f[:-3] for f in os.listdir(bench_dir) if f.endswith(".lm"))
    print(f"{'program':10} {'median s':>9} {'min s':>8}  output")
    for name in names:
        path = os.path.join(bench_dir, name + ".lm")
        times, out = [], ""
        for _ in range(args.runs):
            t0 = time.perf_counter()
            r = subprocess.run([args.lumen, path], capture_output=True, text=True)
            times.append(time.perf_counter() - t0)
            if r.returncode != 0:
                print(f"{name}: failed\n{r.stderr}")
                return 1
            out = r.stdout.strip().replace("\n", " | ")
        print(f"{name:10} {statistics.median(times):9.3f} {min(times):8.3f}  {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
