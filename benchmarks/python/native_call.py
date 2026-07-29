import math

from common import run_benchmark


def benchmark(size: int, variant: str) -> float:
    checksum = 0.0
    if variant == "native":
        for index in range(size):
            checksum += math.fabs((index % 2_001) - 1_000)
    else:
        for index in range(size):
            value = (index % 2_001) - 1_000
            checksum += -value if value < 0 else value
    return checksum


if __name__ == "__main__":
    run_benchmark(
        "native_call",
        benchmark,
        2_000_000,
        variants=("native", "inline"),
        description="Calls across the native-library boundary versus equivalent inline logic.",
    )
