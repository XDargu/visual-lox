from common import run_benchmark


MODULUS = 1_000_000_007


def benchmark(size: int, _: str) -> int:
    previous, current = 0, 1
    for _ in range(size):
        previous, current = current, (previous + current) % MODULUS
    return previous


if __name__ == "__main__":
    run_benchmark("fibonacci_iterative", benchmark, 2_000_000, description="Iterative arithmetic and local assignment.")
