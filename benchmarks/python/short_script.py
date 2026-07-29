from common import run_benchmark


def benchmark(size: int, _: str) -> int:
    value = 1
    for index in range(size):
        value = (value * 31 + index) & 0xFFFFFFFF
    return value


if __name__ == "__main__":
    run_benchmark(
        "short_script",
        benchmark,
        10,
        description="A deliberately tiny workload. Use an external process timer to measure cold startup.",
    )
