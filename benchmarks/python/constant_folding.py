from common import run_benchmark


MODULUS = 1_000_000_007
FOLDED_VALUE = (17 * 23 + 11) % MODULUS


def benchmark(size: int, variant: str) -> int:
    checksum = 0
    if variant == "folded":
        for _ in range(size):
            checksum = (checksum + FOLDED_VALUE) % MODULUS
        return checksum

    left = 17
    right = 23
    offset = 11
    for _ in range(size):
        checksum = (checksum + left * right + offset) % MODULUS
    return checksum


if __name__ == "__main__":
    run_benchmark(
        "constant_folding",
        benchmark,
        2_000_000,
        variants=("folded", "runtime"),
        description="Precomputed constants versus equivalent arithmetic forced to execute at runtime.",
    )
