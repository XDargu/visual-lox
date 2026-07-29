from common import rolling_checksum, run_benchmark


def benchmark(size: int, _: str) -> tuple[int, int]:
    parts: list[str] = []
    lengths: list[int] = []
    for index in range(size):
        value = f" item-{index % 10_000:04d} "
        value = value.strip().replace("-", ":").upper()
        parts.append(value)
        lengths.append(len(value))
    combined = "|".join(parts)
    return len(combined), rolling_checksum(lengths)


if __name__ == "__main__":
    run_benchmark("string_building", benchmark, 200_000, description="Short-string allocation, transformation, and joining.")
