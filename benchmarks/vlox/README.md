# Visual Lox Benchmarks

This folder contains the Visual Lox benchmarks. The benchmark runner uses the same core, standard library, serializer, compiler and VM as the editor and CLI.

## Benchmark Scripts

Each benchmark should declare these global variables:

- `BenchmarkSize`: a non-negative Number. When you use `--size`, the runner replaces the default value of this variable.
- `BenchmarkChecksum`: the result of the benchmark. The runner reads it after every iteration and checks that it has not changed.

Use a single number for the checksum when possible, and keep exact integer calculations at or below `2^53`. For larger calculations, apply modulo `1,000,000,007`. Benchmarks should not print anything while they are being measured.

Benchmark files are stored in `benchmarks/vlox/cases`. Variants use separate files so the benchmark does not need an extra branch to select between them.

`cases/smoke.vlox` is a small graph used to check that the runner works.

The benchmark files are created through the normal graph and serialization APIs in `generateCases.cpp`. After changing their definitions, you can generate them again with:

```powershell
cmake --build build --config Release --target visual-lox-benchmark-generator
build/bin/vlox-benchmark-generator.exe benchmarks/vlox/cases
```

The generator creates these cases:

| Python reference | Generated VLox case(s) | Notes |
| --- | --- | --- |
| `number_loop.py` | `number-loop.vlox` | Sum of squares with the benchmark modulus |
| `fibonacci_iterative.py` | `fibonacci-iterative.vlox` | Iterative Fibonacci with the benchmark modulus |
| `fibonacci_recursive.py` | `fibonacci-recursive.vlox` | Direct recursive Fibonacci |
| `prime_sieve.py` | `prime-sieve.vlox` | Boolean-list sieve; numeric checksum combines prime count and sum |
| `mandelbrot.py` | `mandelbrot.vlox` | Same coordinates, 50-iteration limit and 32-bit checksum |
| `function_calls.py` | `function-calls.vlox` | Calls a pure script `AddModulo` function on every iteration |
| `objects.py` | `objects.vlox` | Creates and keeps a list of `Counter` instances, then reads and writes their properties |
| `list_processing.py` | `list-processing-loop.vlox`, `list-processing-callbacks.vlox` | Explicit loop and callback-node variants; numeric checksum combines output count and sum |
| `string_building.py` | `string-building.vlox` | Builds and keeps transformed parts and lengths, then joins the parts; numeric checksum replaces rolling FNV and number text is not zero-padded |
| `sorting.py` | `sorting.vlox` | Uses the same LCG input and native list sort; additive checksum replaces Python's bitwise FNV checksum |
| `pattern_matching.py` | `pattern-matching.vlox` | Equivalent ordered branch chain for literal, alternative, guarded and default cases |
| `constant_folding.py` | `constant-folding-folded.vlox`, `constant-folding-runtime.vlox` | Precomputed expression and variable-dependent expression graphs |
| `native_call.py` | `native-call-native.vlox`, `native-call-inline.vlox` | `Math::Abs` versus the equivalent `Math::Max(value, -value)` graph |
| `dynamic_values.py` | `dynamic-values-homogeneous.vlox`, `dynamic-values-mixed.vlox` | Homogeneous Number list versus mixed Any list; list length is used as the checksum because VLox has no runtime type-test node |
| `gc_pressure.py` | `gc-pressure.vlox` | Creates an object, string and three-number list per iteration; this is a flattened version of Python's batched allocation workload |
| `short_script.py` | `short-script.vlox` | Small workload for measuring cold start time |
| `multiple_outputs.py` | `multiple-outputs-multiple.vlox`, `multiple-outputs-inline.vlox` | Script function with three outputs versus inline arithmetic |
| `equivalent_forms.py` | `equivalent-forms-direct.vlox`, `equivalent-forms-temporaries.vlox` | Direct expression graph versus explicit temporary variables |

Some checksums are different from their Python versions because of the node types currently available in Visual Lox. These versions still test the same work and give the runner a consistent result to check.

For now, use `--gc off` with `objects.vlox` and `gc-pressure.vlox`. When GC is enabled, creating objects can fail and the allocation test becomes unstable with larger inputs. This is a known runtime issue.

## Building

Configure the project and build the Release runner:

```powershell
cmake -S examples -B build
cmake --build build --config Release --target visual-lox-benchmarks
```

On Windows, the executable is written to `build/bin/vlox-benchmark.exe`.

## Running

This command loads and compiles the script once, then measures its execution:

```powershell
build/bin/vlox-benchmark.exe --size 2000000 --warmup 3 --repeat 10 benchmarks/vlox/cases/number-loop.vlox
```

You can use `--csv` to get rows compatible with the Python benchmark results:

```powershell
build/bin/vlox-benchmark.exe --benchmark number_loop --variant default --size 2000000 --warmup 5 --repeat 20 --csv benchmarks/vlox/cases/number-loop.vlox
```

You can also measure compilation by itself, or compilation and execution together:

```powershell
build/bin/vlox-benchmark.exe --mode compile --no-checksum benchmarks/vlox/cases/number-loop.vlox
build/bin/vlox-benchmark.exe --mode run benchmarks/vlox/cases/number-loop.vlox
```

The runner supports `--folding on|off` and `--gc on|off`. Both values are shown in the normal output. Garbage collection is disabled by default, which matches the current initial state of the VM.

Only use `--no-checksum` for debugging or when measuring compilation. Execution benchmarks should always check `BenchmarkChecksum`.

To include executable startup and file loading in the result, measure the command from outside the process:

```powershell
Measure-Command { build/bin/vlox-benchmark.exe --warmup 0 --repeat 1 benchmarks/vlox/cases/short-script.vlox }
```

Run each benchmark case in a separate process. This prevents the VM globals and heap from one case from affecting another.
