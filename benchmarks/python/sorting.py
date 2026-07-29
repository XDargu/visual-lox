from common import lcg_values, rolling_checksum, run_benchmark


def benchmark(size: int, _: str) -> tuple[int, int, int]:
    values = lcg_values(size)
    values.sort()
    if not values:
        return 0, 0, rolling_checksum(())
    return values[0], values[-1], rolling_checksum(values)


if __name__ == "__main__":
    run_benchmark("sorting", benchmark, 500_000, description="Built-in numeric sorting using deterministic data.")
