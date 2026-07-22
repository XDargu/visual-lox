# Visual Lox

Visual Lox is a general purpose visual scripting language based on [Lox Cpp](https://github.com/XDargu/loxcpp), a simple language I created as an extension of Lox.

The main goal of Visual Lox is offering a way of creating simple scripts for anyone, even if they don't know how to program.

Visual scripting languages have existed for a long time, and they are used extensively in videogames, which is the area where I work. There are powerful visual scripting languages, with [Unreal's Blueprints](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprints-visual-scripting-in-unreal-engine) being probably the most famous one. With Visual Lox, I'm aiming to provide a similar degree of creative freedom, ease of use and simple UX.

The project is currently under development and in early stages, so many of the basic LoxCpp features are not supported.

![Visual Lox Editor](https://github.com/XDargu/visual-lox/blob/main/resources/Visual%20Lox%20Screenshot.png)

## Features

For now, Visual Lox has reached the MVP stage: it is possible to create a simple script on a visual editor and run it.

### Functions

It is possible to create functions, that can of course be recursive. This is an example of the fibonacci function implemented in Visual Lox:

![Fibonacci Function](https://github.com/XDargu/visual-lox/blob/main/resources/Visual%20Lox%20Fibonacci.png)

### Higher-Order Functions

Higher-Order functions are already supported, since they were part of the features supported by LoxCpp. Functions are first-citizens of the language, the same was as in LoxCpp.

This is a simple example of a program that returns all the even values of a list:

![Program that prints all even numbers of a list](https://github.com/XDargu/visual-lox/blob/main/resources/Visual%20Lox%20Is%20Even%20Main%20Graph.png)

The IsEven function is implemented like this:

![IsEven function](https://github.com/XDargu/visual-lox/blob/main/resources/Visual%20Lox%20Is%20Even%20Function.png)

### Global Variables

You can declare, get and set global variables in Visual Lox.

### Expression Folding

Visual Lox is a visual programming language, which means that implementing expression folding is pretty simple without needing to represent the compiled data in an intermediate format, like ASTs. With only the graph information, it is possible to do a pretty decent expression folding.

During compilation, Visual Lox pre-calculates the value of nodes and branches that can be deduced at compile time. When compiling the program, Visual Lox will simply substitute the calls for a constant.

For example, in the IsEven program from the Higher-Order Functions section, the list creation is automatically folded into a constant:

![Comparison between a folded and not folded program](https://github.com/XDargu/visual-lox/blob/main/resources/Visual%20Lox%20Is%20Even%20Compiler%20Folding%20Comparison.png)

### Node Library

There is an increasingly large library of native node in VisualLox to perform math operations, string operations, interact with the file system, or to do IO operations.

While at the moment Visual Lox is on early stages, it is already possible to use it to create small programs similar to what you can do with software like [AutoHotKey](https://www.autohotkey.com/).

![Some of the native nodes of Visual Lox](https://github.com/XDargu/visual-lox/blob/main/resources/Visual%20Lox%20Nodes.png)

## Script Files

Visual Lox scripts can be saved and reopened as `.vlox` files from the editor.

The files use a versioned JSON format.

## Applications and Architecture

Visual Lox is split into reusable layers:

- `lox` contains the bytecode virtual machine.
- `visual-lox-core` contains the graph model, node library, serialization, compilation and script runtime.
- `visual-lox-editor` contains the ImGui editor and file dialogs.
- `visual-lox-cli` is a console runner for `.vlox` files and does not start the editor.
- `visual-lox-tests` exercises the core directly; CTest also runs a CLI smoke test.

Both applications use the same standard node library and the same compile-and-execute pipeline, so a script behaves consistently in the editor and at the command line.

All editor chnges pass through the core `DocumentOperations`.

Use `Ctrl+C` and `Ctrl+V` to copy and paste selected graph nodes or the selected function, variable, function input, or function output. Pasted fragments receive new IDs; links and references within the copied fragment are remapped to those IDs.

Undo and redo are available through the editor buttons or with `Ctrl+Z` and `Ctrl+Y` (`Ctrl+Shift+Z` also redoes).

### Validation and diagnostics

Graphs are validated before compilation.

Errors prevent compilation:
 - In the editor, affected nodes receive a red border, warnings receive a yellow border, and the Compiler tab lists the full diagnostics.
 - The CLI writes the same structured diagnostics to the console and exits with a compilation error code.

### Node flags

Node flags are split by lifetime:
 - `NodeDefinitionFlags`: definition data such as `ReadOnly`, `DynamicInputs`, `Pure` and `Protected`.
 - `NodeInstanceFlags`: per-instance node state such as `Error`.

## Building

Configure the examples and build the editor, CLI and tests:

```sh
cmake -S examples -B build
cmake --build build --config Debug --target visual-lox-editor visual-lox-cli visual-lox-tests
```

On Windows, the Debug executables are written to `build/bin` with a `_d` suffix.

## Running from the Command Line

Run a saved script without opening the editor:

```sh
build/bin/visual-lox-cli_d.exe path/to/script.vlox
```

Use `--disassemble` to print the generated bytecode before executing it:

```sh
build/bin/visual-lox-cli_d.exe --disassemble path/to/script.vlox
```

The CLI returns a non-zero exit code when loading, compilation or execution fails, making it suitable for scripts and CI jobs.

## Tests

Run the test suite after building:

```sh
ctest --test-dir build -C Debug --output-on-failure
```

New core tests can be added to the `visual-lox-tests` target, while end-to-end behavior can be covered with additional CTest entries that invoke the CLI.

## Future Work

- Fully support all LoxCpp features (classes, pattern matching, all flow control nodes, ranges...)
- Improve editor UX
- Module system
