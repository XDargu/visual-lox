from common import run_benchmark


MODULUS = 1_000_000_007


def add(left: int, right: int) -> int:
    return (left + right) % MODULUS


def benchmark(size: int, _: str) -> int:
    result = 0
    for value in range(size):
        result = add(result, value)
    return result


if __name__ == "__main__":
    run_benchmark("function_calls", benchmark, 2_000_000, description="Repeated calls to a tiny user-defined function.")
