from common import run_benchmark


MODULUS = 1_000_000_007


def benchmark(size: int, _: str) -> int:
    total = 0
    for value in range(1, size + 1):
        total = (total + value * value) % MODULUS
    return total


if __name__ == "__main__":
    run_benchmark("number_loop", benchmark, 2_000_000, description="Arithmetic, local variables, loop branches, and interpreter dispatch.")
