from common import run_benchmark


class Node:
    def __init__(self, value: int) -> None:
        self.value = value
        self.next: Node | None = None


def benchmark(size: int, _: str) -> int:
    checksum = 0
    for start in range(0, size, 1_000):
        count = min(1_000, size - start)
        nodes = [Node(start + index) for index in range(count)]
        strings = [f"temporary-{node.value}" for node in nodes]
        lists = [[node.value, node.value + 1, node.value + 2] for node in nodes]
        for index in range(len(nodes) - 1):
            nodes[index].next = nodes[index + 1]
        checksum = (checksum + len(strings[-1]) + lists[-1][-1] + nodes[-1].value) & 0xFFFFFFFF
    return checksum


if __name__ == "__main__":
    run_benchmark("gc_pressure", benchmark, 200_000, description="Repeated temporary object, string, and list allocation.")
