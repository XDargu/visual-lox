from functools import reduce

from common import lcg_values, run_benchmark


MODULUS = 1_000_000_007


def benchmark(size: int, variant: str) -> tuple[int, int]:
    values = [value % 1_000_000 for value in lcg_values(size)]
    if variant == "callbacks":
        transformed = map(lambda value: (value * 3 + 1) % MODULUS, values)
        selected = filter(lambda value: value % 2 == 0, transformed)
        return reduce(lambda result, value: (result[0] + 1, (result[1] + value) % MODULUS), selected, (0, 0))

    count = 0
    result = 0
    for value in values:
        transformed = (value * 3 + 1) % MODULUS
        if transformed % 2 == 0:
            count += 1
            result = (result + transformed) % MODULUS
    return count, result


if __name__ == "__main__":
    run_benchmark(
        "list_processing",
        benchmark,
        500_000,
        variants=("loop", "callbacks"),
        description="Map/filter/reduce-style list processing using an explicit loop or higher-order functions.",
    )
