from common import rolling_checksum, run_benchmark


def benchmark(size: int, _: str) -> tuple[int, int]:
    if size < 2:
        return 0, rolling_checksum(())

    prime = bytearray(b"\x01") * (size + 1)
    prime[0:2] = b"\x00\x00"
    candidate = 2
    while candidate * candidate <= size:
        if prime[candidate]:
            multiple = candidate * candidate
            while multiple <= size:
                prime[multiple] = 0
                multiple += candidate
        candidate += 1

    primes = (value for value in range(2, size + 1) if prime[value])
    count = sum(prime)
    return count, rolling_checksum(primes)


if __name__ == "__main__":
    run_benchmark("prime_sieve", benchmark, 250_000, description="List indexing, loops, arithmetic, and branches.")
