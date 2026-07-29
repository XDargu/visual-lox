from common import run_benchmark


def fibonacci(value: int) -> int:
    if value < 2:
        return value
    return fibonacci(value - 1) + fibonacci(value - 2)


def benchmark(size: int, _: str) -> int:
    return fibonacci(size)


if __name__ == "__main__":
    run_benchmark("fibonacci_recursive", benchmark, 30, description="Recursive function-call and branch overhead.")
