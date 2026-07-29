from common import run_benchmark


def split_value(value: int) -> tuple[int, int, int]:
    return value + 1, value + 2, value + 3


def benchmark(size: int, variant: str) -> int:
    checksum = 0
    if variant == "multiple":
        for value in range(size):
            first, second, third = split_value(value)
            checksum = (checksum + first + second + third) & 0xFFFFFFFF
    else:
        for value in range(size):
            checksum = (checksum + (value + 1) + (value + 2) + (value + 3)) & 0xFFFFFFFF
    return checksum


if __name__ == "__main__":
    run_benchmark(
        "multiple_outputs",
        benchmark,
        2_000_000,
        variants=("multiple", "inline"),
        description="Packaging and unpacking several function outputs versus inline calculations.",
    )
