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

## Future Work
- Allow saving and loading scripts
- Fully support all LoxCpp features (classes, pattern matching, all flow control nodes, ranges...)
- Improve editor UX
- Module system