from common import run_benchmark


MODULUS = 1_000_000_007


class Counter:
    def __init__(self, value: int) -> None:
        self.value = value
        self.updates = 0


def benchmark(size: int, _: str) -> int:
    objects = [Counter(index) for index in range(size)]
    checksum = 0
    for item in objects:
        item.value = (item.value * 3 + 1) % MODULUS
        item.updates += 1
        checksum = (checksum + item.value + item.updates) % MODULUS
    return checksum


if __name__ == "__main__":
    run_benchmark("objects", benchmark, 200_000, description="Object allocation and repeated property reads and writes.")
