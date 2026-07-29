"""Shared command-line harness and deterministic utilities for Python benchmarks."""

from __future__ import annotations

import argparse
import csv
import gc
import math
import statistics
import sys
import time
from collections.abc import Callable, Iterable
from typing import Any

Benchmark = Callable[[int, str], Any]


def lcg_values(count: int, seed: int = 0xC0FFEE) -> list[int]:
    """Return values from an LCG that is straightforward to reproduce in VLox and JS."""
    state = seed & 0xFFFFFFFF
    values: list[int] = []
    for _ in range(count):
        state = (1664525 * state + 1013904223) & 0xFFFFFFFF
        values.append(state)
    return values


def rolling_checksum(values: Iterable[int]) -> int:
    checksum = 2166136261
    for value in values:
        checksum = ((checksum ^ (value & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    return checksum


def run_benchmark(
    name: str,
    benchmark: Benchmark,
    default_size: int,
    *,
    variants: tuple[str, ...] = ("default",),
    description: str = "",
) -> None:
    parser = argparse.ArgumentParser(description=description or name)
    parser.add_argument("--size", type=int, default=default_size, help=f"Workload size (default: {default_size})")
    parser.add_argument("--warmup", type=int, default=3, help="Untimed warm-up runs")
    parser.add_argument("--repeat", type=int, default=10, help="Number of measured runs")
    parser.add_argument("--variant", choices=variants, default=variants[0], help="Workload implementation to run")
    parser.add_argument("--csv", action="store_true", help="Write one CSV row per measured run")
    args = parser.parse_args()

    if args.size < 0 or args.warmup < 0 or args.repeat < 1:
        parser.error("--size and --warmup must be non-negative; --repeat must be positive")

    expected = benchmark(args.size, args.variant)
    for _ in range(args.warmup):
        if benchmark(args.size, args.variant) != expected:
            raise RuntimeError("Benchmark produced a non-deterministic checksum during warm-up")

    samples_ns: list[int] = []
    for _ in range(args.repeat):
        gc.collect()
        started = time.perf_counter_ns()
        actual = benchmark(args.size, args.variant)
        elapsed = time.perf_counter_ns() - started
        if actual != expected:
            raise RuntimeError(f"Checksum changed: expected {expected!r}, received {actual!r}")
        samples_ns.append(elapsed)

    if args.csv:
        writer = csv.writer(sys.stdout, lineterminator="\n")
        writer.writerow(("benchmark", "language", "variant", "size", "iteration", "time_ns", "checksum"))
        for iteration, elapsed in enumerate(samples_ns, start=1):
            writer.writerow((name, "python", args.variant, args.size, iteration, elapsed, repr(expected)))
        return

    samples_ms = [sample / 1_000_000 for sample in samples_ns]
    ordered = sorted(samples_ms)
    p95_index = max(0, math.ceil(len(ordered) * 0.95) - 1)
    print(f"benchmark={name} language=python variant={args.variant} size={args.size} checksum={expected!r}")
    print(
        f"runs={args.repeat} median_ms={statistics.median(samples_ms):.6f} "
        f"min_ms={min(samples_ms):.6f} p95_ms={ordered[p95_index]:.6f}"
    )
