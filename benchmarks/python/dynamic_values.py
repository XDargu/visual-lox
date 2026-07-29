from common import run_benchmark


def benchmark(size: int, variant: str) -> tuple[int, int, int]:
    if variant == "homogeneous":
        values: list[object] = [index % 1_000 for index in range(size)]
    else:
        values = []
        for index in range(size):
            selector = index % 4
            if selector == 0:
                values.append(index % 1_000)
            elif selector == 1:
                values.append(f"v{index % 1_000}")
            elif selector == 2:
                values.append(index % 2 == 0)
            else:
                values.append(None)

    numbers = 0
    strings = 0
    others = 0
    for value in values:
        if type(value) is int:
            numbers += value
        elif isinstance(value, str):
            strings += len(value)
        else:
            others += 1
    return numbers, strings, others


if __name__ == "__main__":
    run_benchmark(
        "dynamic_values",
        benchmark,
        500_000,
        variants=("homogeneous", "mixed"),
        description="Homogeneous numeric lists versus lists containing several dynamic value types.",
    )
