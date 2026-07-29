"""Benchmark CPython against VLox and print a median-time comparison table."""

from __future__ import annotations

import argparse
import ast
import csv
import io
import math
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


MODULUS = 1_000_000_007
UINT32_MODULUS = 1 << 32


@dataclass(frozen=True)
class Case:
    name: str
    python_file: str
    variant: str
    smoke_size: int
    vlox_file: str
    contract: str = "exact"
    partially_comparable: bool = False

    @property
    def key(self) -> str:
        return f"{self.name}:{self.variant}"


CASES = (
    Case("number_loop", "number_loop.py", "default", 20_000, "number-loop.vlox"),
    Case("fibonacci_iterative", "fibonacci_iterative.py", "default", 20_000, "fibonacci-iterative.vlox"),
    Case("fibonacci_recursive", "fibonacci_recursive.py", "default", 20, "fibonacci-recursive.vlox"),
    Case("prime_sieve", "prime_sieve.py", "default", 5_000, "prime-sieve.vlox", "prime"),
    Case("mandelbrot", "mandelbrot.py", "default", 20, "mandelbrot.vlox"),
    Case("function_calls", "function_calls.py", "default", 20_000, "function-calls.vlox"),
    Case("objects", "objects.py", "default", 2_000, "objects.vlox"),
    Case("list_processing", "list_processing.py", "loop", 5_000, "list-processing-loop.vlox", "encoded_tuple"),
    Case("list_processing", "list_processing.py", "callbacks", 5_000, "list-processing-callbacks.vlox", "encoded_tuple"),
    Case("string_building", "string_building.py", "default", 2_000, "string-building.vlox", "string", True),
    Case("sorting", "sorting.py", "default", 5_000, "sorting.vlox", "sorting", True),
    Case("pattern_matching", "pattern_matching.py", "default", 20_000, "pattern-matching.vlox"),
    Case("constant_folding", "constant_folding.py", "folded", 20_000, "constant-folding-folded.vlox"),
    Case("constant_folding", "constant_folding.py", "runtime", 20_000, "constant-folding-runtime.vlox"),
    Case("native_call", "native_call.py", "native", 20_000, "native-call-native.vlox"),
    Case("native_call", "native_call.py", "inline", 20_000, "native-call-inline.vlox"),
    Case("dynamic_values", "dynamic_values.py", "homogeneous", 5_000, "dynamic-values-homogeneous.vlox", "length", True),
    Case("dynamic_values", "dynamic_values.py", "mixed", 5_000, "dynamic-values-mixed.vlox", "length", True),
    Case("gc_pressure", "gc_pressure.py", "default", 2_000, "gc-pressure.vlox", "gc_pressure", True),
    Case("short_script", "short_script.py", "default", 10, "short-script.vlox"),
    Case("multiple_outputs", "multiple_outputs.py", "multiple", 20_000, "multiple-outputs-multiple.vlox"),
    Case("multiple_outputs", "multiple_outputs.py", "inline", 20_000, "multiple-outputs-inline.vlox"),
    Case("equivalent_forms", "equivalent_forms.py", "direct", 20_000, "equivalent-forms-direct.vlox"),
    Case("equivalent_forms", "equivalent_forms.py", "temporaries", 20_000, "equivalent-forms-temporaries.vlox"),
)


def find_vlox_runner(repository: Path, requested: Path | None) -> Path:
    if requested:
        runner = requested.resolve()
        if runner.is_file():
            return runner
        raise FileNotFoundError(f"VLox benchmark runner does not exist: {runner}")

    candidates = (
        repository / "build/bin/vlox-benchmark.exe",
        repository / "build/bin/vlox-benchmark",
        repository / "build/bin/Release/vlox-benchmark.exe",
        repository / "build/bin/Release/vlox-benchmark",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("Could not find vlox-benchmark. Build visual-lox-benchmarks or pass --vlox-runner.")


def run_csv(command: list[str], expected_rows: int) -> list[dict[str, str]]:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        details = result.stderr.strip() or result.stdout.strip() or f"exit code {result.returncode}"
        raise RuntimeError(f"Command failed:\n  {' '.join(command)}\n{details}")

    rows = list(csv.DictReader(io.StringIO(result.stdout)))
    if len(rows) != expected_rows:
        raise RuntimeError(f"Expected {expected_rows} CSV rows from {' '.join(command)}, received {len(rows)}.\n{result.stdout}")
    return rows


def prime_checksum(size: int) -> int:
    if size < 2:
        return 0

    flags = bytearray(b"\x01") * (size + 1)
    flags[0:2] = b"\x00\x00"
    candidate = 2
    while candidate * candidate <= size:
        if flags[candidate]:
            for multiple in range(candidate * candidate, size + 1, candidate):
                flags[multiple] = 0
        candidate += 1
    return sum(flags) * MODULUS + sum(index for index, is_prime in enumerate(flags) if is_prime)


def sorting_checksum(size: int) -> int:
    state = 0xC0FFEE
    checksum = 0
    for _ in range(size):
        state = (1_664_525 * state + 1_013_904_223) % UINT32_MODULUS
        checksum = (checksum + state) % MODULUS
    return checksum


def string_checksum(size: int) -> int:
    length_sum = sum(len(f"ITEM:{index % 10_000}") for index in range(size))
    combined_length = length_sum + max(0, size - 1)
    return combined_length * MODULUS + length_sum


def expected_vlox_checksum(case: Case, python_checksum: str, size: int) -> float:
    python_value = ast.literal_eval(python_checksum)
    if case.contract == "exact":
        return float(python_value)
    if case.contract == "encoded_tuple":
        count, total = python_value
        return float(count * MODULUS + total)
    if case.contract == "prime":
        return float(prime_checksum(size))
    if case.contract == "sorting":
        return float(sorting_checksum(size))
    if case.contract == "string":
        return float(string_checksum(size))
    if case.contract == "length":
        return float(size)
    if case.contract == "gc_pressure":
        return float(max(0, size - 1))
    raise ValueError(f"Unknown checksum contract: {case.contract}")


def verify_checksum(case: Case, python_checksum: str, vlox_checksum: str, size: int) -> None:
    expected = expected_vlox_checksum(case, python_checksum, size)
    actual = float(vlox_checksum)
    if actual != expected:
        raise RuntimeError(
            f"Checksum mismatch for {case.key}: Python={python_checksum}, VLox={vlox_checksum}, expected VLox={expected:g}"
        )


def winner(python_ms: float, vlox_ms: float) -> str:
    if python_ms == vlox_ms:
        return "Tie"
    if python_ms < vlox_ms:
        return f"Python {vlox_ms / python_ms:.2f}x"
    return f"VLox {python_ms / vlox_ms:.2f}x"


def print_table(results: list[dict[str, object]]) -> None:
    print("| Benchmark | Variant | Size | Python ms | VLox ms | Faster |")
    print("| --- | --- | ---: | ---: | ---: | --- |")
    for result in results:
        marker = "*" if result["partial"] else ""
        print(
            f"| {result['name']}{marker} | {result['variant']} | {result['size']} | "
            f"{result['python_ms']:.6f} | {result['vlox_ms']:.6f} | {result['winner']} |"
        )


def select_cases(filters: list[str]) -> list[Case]:
    if not filters:
        return list(CASES)

    selected = [case for case in CASES if case.name in filters or case.key in filters]
    unknown = sorted(set(filters) - {value for case in selected for value in (case.name, case.key)})
    if unknown:
        raise ValueError(f"Unknown --case value(s): {', '.join(unknown)}")
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--warmup", type=int, default=3, help="Untimed iterations per language (default: 3)")
    parser.add_argument("--repeat", type=int, default=10, help="Measured iterations per language (default: 10)")
    parser.add_argument("--full", action="store_true", help="Use each benchmark's full default size instead of its smoke size")
    parser.add_argument("--case", action="append", default=[], help="Run a benchmark name or name:variant; may be repeated")
    parser.add_argument("--gc", choices=("on", "off"), default="off", help="VLox garbage collection setting (default: off)")
    parser.add_argument("--folding", choices=("on", "off"), default="on", help="VLox constant-folding setting (default: on)")
    parser.add_argument("--vlox-runner", type=Path, help="Path to the vlox-benchmark executable")
    args = parser.parse_args()

    if args.warmup < 0 or args.repeat < 1:
        parser.error("--warmup must be non-negative and --repeat must be positive")

    repository = Path(__file__).resolve().parent.parent
    python_directory = repository / "benchmarks/python"
    vlox_directory = repository / "benchmarks/vlox/cases"

    try:
        cases = select_cases(args.case)
        vlox_runner = find_vlox_runner(repository, args.vlox_runner)
        results: list[dict[str, object]] = []

        for case in cases:
            print(f"Running {case.key}...", file=sys.stderr, flush=True)
            python_command = [
                sys.executable,
                str(python_directory / case.python_file),
                "--variant",
                case.variant,
                "--warmup",
                str(args.warmup),
                "--repeat",
                str(args.repeat),
                "--csv",
            ]
            vlox_command = [
                str(vlox_runner),
                "--benchmark",
                case.name,
                "--variant",
                case.variant,
                "--warmup",
                str(args.warmup),
                "--repeat",
                str(args.repeat),
                "--gc",
                args.gc,
                "--folding",
                args.folding,
                "--csv",
                str(vlox_directory / case.vlox_file),
            ]
            if not args.full:
                python_command.extend(("--size", str(case.smoke_size)))
                vlox_command[1:1] = ("--size", str(case.smoke_size))

            python_rows = run_csv(python_command, args.repeat)
            vlox_rows = run_csv(vlox_command, args.repeat)
            size = int(python_rows[0]["size"])
            if int(vlox_rows[0]["size"]) != size:
                raise RuntimeError(f"Size mismatch for {case.key}: Python={size}, VLox={vlox_rows[0]['size']}")

            verify_checksum(case, python_rows[0]["checksum"], vlox_rows[0]["checksum"], size)
            python_ms = statistics.median(int(row["time_ns"]) for row in python_rows) / 1_000_000
            vlox_ms = statistics.median(int(row["time_ns"]) for row in vlox_rows) / 1_000_000
            results.append(
                {
                    "name": case.name,
                    "variant": case.variant,
                    "size": size,
                    "python_ms": python_ms,
                    "vlox_ms": vlox_ms,
                    "winner": winner(python_ms, vlox_ms),
                    "partial": case.partially_comparable,
                }
            )

        print()
        print(f"Configuration: warmup={args.warmup}, repeat={args.repeat}, VLox GC={args.gc}, folding={args.folding}")
        print_table(results)

        comparable = [result for result in results if not result["partial"]]
        if comparable:
            ratios = [result["python_ms"] / result["vlox_ms"] for result in comparable]
            geometric_speedup = math.exp(statistics.fmean(math.log(ratio) for ratio in ratios))
            if geometric_speedup >= 1:
                print(f"\nAcross {len(comparable)} fully comparable variants, VLox was {geometric_speedup:.2f}x faster by geometric mean.")
            else:
                print(f"\nAcross {len(comparable)} fully comparable variants, Python was {1 / geometric_speedup:.2f}x faster by geometric mean.")
        if any(result["partial"] for result in results):
            print("* Partially comparable workload; see benchmarks/vlox/README.md for the contract difference.")
        return 0
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        print(f"Comparison error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
