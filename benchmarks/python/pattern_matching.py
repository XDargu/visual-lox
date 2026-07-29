from common import run_benchmark


def classify(value: int) -> int:
    match value:
        case 0:
            return 11
        case 1 | 2:
            return 17
        case value if value < 10:
            return 23
        case value if value < 100:
            return 31
        case _:
            return 47


def benchmark(size: int, _: str) -> int:
    checksum = 0
    for index in range(size):
        checksum = (checksum + classify(index % 128)) & 0xFFFFFFFF
    return checksum


if __name__ == "__main__":
    run_benchmark("pattern_matching", benchmark, 2_000_000, description="Repeated literal, alternative, guarded, and default pattern matching.")
