from common import run_benchmark


MODULUS = 1_000_000_007


def benchmark(size: int, variant: str) -> int:
    checksum = 0
    if variant == "direct":
        for value in range(size):
            checksum = (checksum + (value * 3 + 1) * 5) % MODULUS
    else:
        for value in range(size):
            multiplied = value * 3
            offset = multiplied + 1
            result = offset * 5
            checksum = (checksum + result) % MODULUS
    return checksum


if __name__ == "__main__":
    run_benchmark(
        "equivalent_forms",
        benchmark,
        2_000_000,
        variants=("direct", "temporaries"),
        description="Equivalent expression shapes; a proxy for comparing different VLox graph layouts.",
    )
