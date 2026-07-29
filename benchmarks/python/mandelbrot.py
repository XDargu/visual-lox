from common import run_benchmark


MAX_ITERATIONS = 50


def benchmark(size: int, _: str) -> int:
    if size == 0:
        return 0

    checksum = 0
    for py in range(size):
        cy = -1.25 + 2.5 * py / size
        for px in range(size):
            cx = -2.0 + 3.0 * px / size
            x = 0.0
            y = 0.0
            iterations = 0
            while x * x + y * y <= 4.0 and iterations < MAX_ITERATIONS:
                x, y = x * x - y * y + cx, 2.0 * x * y + cy
                iterations += 1
            checksum = (checksum + iterations) & 0xFFFFFFFF
    return checksum


if __name__ == "__main__":
    run_benchmark("mandelbrot", benchmark, 200, description="Nested loops, floating-point arithmetic, and branches without rendering.")
