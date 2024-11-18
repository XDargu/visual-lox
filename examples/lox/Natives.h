#ifndef loxcpp_natives_h
#define loxcpp_natives_h

#include "Value.h"

class VM;

// Utility
Value clock(int argCount, Value* args, VM* vm);
Value sizeOf(int argCount, Value* args, VM* vm);

// Types
Value isList(int argCount, Value* args, VM* vm);
Value inBounds(int argCount, Value* args, VM* vm);

// IO
Value readInput(int argCount, Value* args, VM* vm);
Value readFile(int argCount, Value* args, VM* vm);
Value writeFile(int argCount, Value* args, VM* vm);

// Lists
Value push(int argCount, Value* args, VM* vm);
Value pop(int argCount, Value* args, VM* vm);
Value erase(int argCount, Value* args, VM* vm);
Value concat(int argCount, Value* args, VM* vm);

// Iterables
Value contains(int argCount, Value* args, VM* vm);
Value indexOf(int argCount, Value* args, VM* vm);
Value findIf(int argCount, Value* args, VM* vm);
Value map(int argCount, Value* args, VM* vm);
Value filter(int argCount, Value* args, VM* vm);
Value reduce(int argCount, Value* args, VM* vm);

void registerNatives(VM* vm);

#endif