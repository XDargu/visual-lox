"""Run every Python benchmark as a separate process."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


BENCHMARKS = (
    ("number_loop.py", 20_000, ("default",)),
    ("fibonacci_iterative.py", 20_000, ("default",)),
    ("fibonacci_recursive.py", 20, ("default",)),
    ("prime_sieve.py", 5_000, ("default",)),
    ("mandelbrot.py", 20, ("default",)),
    ("function_calls.py", 20_000, ("default",)),
    ("objects.py", 2_000, ("default",)),
    ("list_processing.py", 5_000, ("loop", "callbacks")),
    ("string_building.py", 2_000, ("default",)),
    ("sorting.py", 5_000, ("default",)),
    ("pattern_matching.py", 20_000, ("default",)),
    ("constant_folding.py", 20_000, ("folded", "runtime")),
    ("native_call.py", 20_000, ("native", "inline")),
    ("dynamic_values.py", 5_000, ("homogeneous", "mixed")),
    ("gc_pressure.py", 2_000, ("default",)),
    ("short_script.py", 10, ("default",)),
    ("multiple_outputs.py", 20_000, ("multiple", "inline")),
    ("equivalent_forms.py", 20_000, ("direct", "temporaries")),
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repeat", type=int, default=3, help="Measured runs per benchmark")
    parser.add_argument("--warmup", type=int, default=1, help="Warm-up runs per benchmark")
    parser.add_argument("--full", action="store_true", help="Use each benchmark's full default size")
    args = parser.parse_args()

    directory = Path(__file__).resolve().parent
    for filename, smoke_size, variants in BENCHMARKS:
        for variant in variants:
            command = [
                sys.executable,
                str(directory / filename),
                "--repeat",
                str(args.repeat),
                "--warmup",
                str(args.warmup),
                "--variant",
                variant,
            ]
            if not args.full:
                command.extend(("--size", str(smoke_size)))
            print(f"\n== {filename} ({variant}) ==", flush=True)
            result = subprocess.run(command, check=False)
            if result.returncode != 0:
                return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
