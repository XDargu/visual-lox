# Python Benchmarks

This folder contains the Python versions of the Visual Lox benchmarks. They only use the standard library and can be used to compare Visual Lox with CPython and, later, JavaScript.

Each benchmark returns the same result every time. This result is also used to check that the equivalent Visual Lox and JavaScript versions are doing the same work.

## Running

You can compare all the Python and Visual Lox benchmarks with:

```powershell
python benchmarks/compare.py
```

By default, this runs 3 warm-ups and 10 measured iterations with the smaller smoke-test inputs. Visual Lox garbage collection is disabled. The runner checks the results before printing a Markdown table.

Use `--help` to see the options for selecting cases, using the full input sizes, changing GC or folding settings, and setting a custom path to the Visual Lox
runner.

You can also run one benchmark directly:

```powershell
python benchmarks/python/number_loop.py
python benchmarks/python/list_processing.py --variant callbacks --size 1000000 --warmup 5 --repeat 20
```

To quickly run the whole suite, including all variants, use:

```powershell
python benchmarks/python/run_all.py
```

Add `--full` to use the normal input size of every benchmark.

Each script supports these options:

- `--size N`: sets the workload size. Its exact meaning depends on the benchmark.
- `--warmup N`: sets the number of untimed runs before measuring.
- `--repeat N`: sets the number of measured runs.
- `--variant NAME`: selects an implementation variant when the benchmark has more than one.
- `--csv`: prints one machine-readable row for every measured run.

Before measuring, the test runs once to get the expected result. It then runs the requested warm-ups, calls `gc.collect()` outside each measured section and measures the benchmark with `time.perf_counter_ns()`.

## Workloads

| Script | What it tests |
| --- | --- |
| `number_loop.py` | Arithmetic, local variables, loop branches and VM dispatch |
| `fibonacci_iterative.py` | Iterative arithmetic |
| `fibonacci_recursive.py` | Recursion and function calls |
| `prime_sieve.py` | Indexed storage and branches |
| `mandelbrot.py` | Nested loops and floating-point arithmetic |
| `function_calls.py` | Small user-defined function calls |
| `objects.py` | Object creation and property access |
| `list_processing.py` | Explicit loops versus map/filter/reduce callbacks |
| `string_building.py` | String creation and transformation |
| `sorting.py` | Built-in/native sorting |
| `pattern_matching.py` | Literal, alternative, guarded and default patterns |
| `constant_folding.py` | Precomputed values versus runtime arithmetic |
| `native_call.py` | Native-library call versus inline logic |
| `dynamic_values.py` | Homogeneous versus mixed-type collections |
| `gc_pressure.py` | Temporary objects and garbage collection pressure |
| `short_script.py` | Small workload for measuring cold start time |
| `multiple_outputs.py` | Packaging and unpacking multiple values |
| `equivalent_forms.py` | Equivalent expression shapes, used as a graph-layout comparison |

## Comparing Results

Use a Release build of Visual Lox. Keep the algorithms, input sizes, constants and checksums the same in every language. Benchmarks should not print anything while they are being measured.

Results measured inside the process only include warm execution. To include loading and runtime startup, measure a new process from the outside. On Windows, you can use:

```powershell
Measure-Command { python benchmarks/python/short_script.py --warmup 0 --repeat 1 }
Measure-Command { build/bin/Release/vlox.exe path/to/short-script.vlox }
```
