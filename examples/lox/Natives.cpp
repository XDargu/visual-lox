#include "Natives.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdarg>
#include <cmath>
#include <time.h>

#include "Vm.h"
#include "Debug.h"
#include "Compiler.h"
#include "Object.h"
#include "VMUtils.h"

Value clock(int argCount, Value* args, VM* vm)
{
    return Value((double)clock() / CLOCKS_PER_SEC);
}

Value sizeOf(int argCount, Value* args, VM* vm)
{
    return Value((double)sizeOf(args[0]));
}

Value isList(int argCount, Value* args, VM* vm)
{
    return Value(isList(args[0]));
}

Value inBounds(int argCount, Value* args, VM* vm)
{
    if (!isNumber(args[1]))
        return Value();

    const int idx = static_cast<int>(asNumber(args[1]));

    if (isRange(args[0]))
    {
        ObjRange* range = asRange(args[0]);
        return Value(range->isInBounds(idx));
    }
    else if (isList(args[0]))
    {
        ObjList* list = asList(args[0]);
        return Value(list->isInBounds(idx));
    }
    else if (isString(args[0]))
    {
        ObjString* str = asString(args[0]);
        return Value(idx >= 0 && idx < str->chars.length());
    }

    return Value();
}

Value readInput(int argCount, Value* args, VM* vm)
{
    std::string line;
    std::getline(std::cin, line);

    return Value(takeString(line.c_str(), line.length()));
}

Value readFile(int argCount, Value* args, VM* vm)
{
    if (isString(args[0]))
    {
        ObjString* fileName = asString(args[0]);
        std::ifstream fileStream(fileName->chars);
        std::stringstream buffer;
        buffer << fileStream.rdbuf();
        fileStream.close();
        return Value(takeString(buffer.str().c_str(), buffer.str().length()));
    }

    return Value(takeString("", 0));
}

Value writeFile(int argCount, Value* args, VM* vm)
{
    if (isString(args[0]) && isString(args[1]))
    {
        ObjString* fileName = asString(args[0]);
        ObjString* content = asString(args[1]);

        std::ofstream fileStream(fileName->chars.c_str());
        if (fileStream.is_open())
        {
            fileStream.write(content->chars.c_str(), content->chars.length());
        }
        fileStream.close();
    }

    return Value();
}

Value push(int argCount, Value* args, VM* vm)
{
    if (!isList(args[0]))
    {
        return Value();
    }
    ObjList* list = asList(args[0]);
    Value item = args[1];
    list->append(item);
    return Value(static_cast<double>(list->items.size()));
}

Value pop(int argCount, Value* args, VM* vm)
{
    if (!isList(args[0]))
    {
        return Value();
    }
    ObjList* list = asList(args[0]);

    if (list->items.size() == 0)
    {
        return Value();
    }

    Value value = *list->items.end();
    list->items.pop_back();
    return value;
}

Value erase(int argCount, Value* args, VM* vm)
{
    if (!isList(args[0]) || !isNumber(args[1]))
    {
        return Value();
    }

    ObjList* list = asList(args[0]);
    const int index = static_cast<int>(asNumber(args[1]));

    if (index < 0 || index >= list->items.size())
    {
        return Value();
    }

    list->deleteValue(index);
    return Value();
}

Value concat(int argCount, Value* args, VM* vm)
{
    if (!isList(args[0]) || !isList(args[1]))
    {
        return Value();
    }

    ObjList* left = asList(args[0]);
    ObjList* right = asList(args[1]);

    ObjList* concat = newList();

    concat->items.reserve(left->items.size() + right->items.size());
    std::copy(left->items.begin(), left->items.end(), std::back_inserter(concat->items));
    std::copy(right->items.begin(), right->items.end(), std::back_inserter(concat->items));

    return Value(concat);
}

Value contains(int argCount, Value* args, VM* vm)
{
    if (!isIterable(args[0]))
    {
        return Value();
    }

    if (isRange(args[0]))
    {
        if (!isNumber(args[1]))
            return Value();

        ObjRange* range = asRange(args[0]);

        for (int idx = 0; range->isInBounds(idx); ++idx)
        {
            if (asNumber(args[1]) == range->getValue(idx))
                return Value(false);
        }
    }
    else if (isList(args[0]))
    {
        ObjList* list = asList(args[0]);
        for (int idx = 0; list->isInBounds(idx); ++idx)
        {
            if (list->getValue(idx) == args[1])
                return Value(true);
        }
    }
    else if (isString(args[0]))
    {
        if (!isString(args[1]))
            return Value();

        if (asString(args[1])->chars.length() != 1)
            return Value();

        ObjString* str = asString(args[0]);

        for (int idx = 0; idx < str->chars.size(); ++idx)
        {
            if (str->chars[idx] == *asString(args[1])->chars.begin())
                return Value(true);
        }
    }

    return Value(false);
}

Value indexOf(int argCount, Value* args, VM* vm)
{
    if (!isIterable(args[0]))
    {
        return Value();
    }

    if (isRange(args[0]))
    {
        if (!isNumber(args[1]))
            return Value();

        ObjRange* range = asRange(args[0]);

        for (int idx = 0; range->isInBounds(idx); ++idx)
        {
            if (asNumber(args[1]) == range->getValue(idx))
                return Value(static_cast<double>(idx));
        }
    }
    else if (isList(args[0]))
    {
        ObjList* list = asList(args[0]);
        for (int idx = 0; list->isInBounds(idx); ++idx)
        {
            if (list->getValue(idx) == args[1])
                return Value(static_cast<double>(idx));
        }
    }
    else if (isString(args[0]))
    {
        if (!isString(args[1]))
            return Value();

        if (asString(args[1])->chars.length() != 1)
            return Value();

        ObjString* str = asString(args[0]);

        for (int idx = 0; idx < str->chars.size(); ++idx)
        {
            if (str->chars[idx] == *asString(args[1])->chars.begin())
                return Value(static_cast<double>(idx));
        }
    }

    return Value();
}

Value findIf(int argCount, Value* args, VM* vm)
{
    if (!isIterable(args[0]) || !isCallable(args[1]))
    {
        return Value();
    }

    Value foundResult;

    forEachIterable(args[0], [&](const Value& element, int idx)
    {
        if (!isFalsey(callFunction(vm, args[1], element)))
        {
            foundResult = element;
            return false;
        }
        return true;
    });

    return foundResult;
}

Value map(int argCount, Value* args, VM* vm)
{
    if (!isIterable(args[0]) || !isCallable(args[1]))
    {
        return Value();
    }
    ObjList* mappedList = newList();

    forEachIterable(args[0], [&](const Value& element, int idx)
    {
        mappedList->append(callFunction(vm, args[1], element));
        return true;
    });

    return Value(mappedList);
}

Value filter(int argCount, Value* args, VM* vm)
{
    if (!isIterable(args[0]) || !isCallable(args[1]))
    {
        return Value();
    }

    ObjList* mappedList = newList();

    forEachIterable(args[0], [&](const Value& element, int idx)
    {
        if (!isFalsey(callFunction(vm, args[1], element)))
        {
            mappedList->append(element);
        }
        return true;
    });

    return Value(mappedList);
}

Value reduce(int argCount, Value* args, VM* vm)
{
    if (!isIterable(args[0]) || !isCallable(args[1]))
    {
        return Value();
    }

    Value accum = args[2];

    forEachIterable(args[0], [&](const Value& element, int idx)
    {
        accum = callFunction(vm, args[1], accum, element);
        return true;
    });

    return accum;
}

void registerNatives(VM* vm)
{
    vm->defineNative("clock", 1, &clock);
    vm->defineNative("sizeOf", 1, &sizeOf);

    // Types
    vm->defineNative("isList", 1, &isList);
    vm->defineNative("inBounds", 1, &inBounds);

    // IO
    vm->defineNative("readInput", 0, &readInput);
    vm->defineNative("readFile", 1, &readFile);
    vm->defineNative("writeFile", 2, &writeFile);

    // Lists
    vm->defineNative("push", 2, &push);
    vm->defineNative("pop", 1, &pop);
    vm->defineNative("erase", 2, &erase);
    vm->defineNative("concat", 2, &concat);

    // Iterables
    vm->defineNative("contains", 2, &contains);
    vm->defineNative("indexOf", 2, &indexOf);
    vm->defineNative("findIf", 2, &findIf);
    vm->defineNative("map", 2, &map);
    vm->defineNative("filter", 2, &filter);
    vm->defineNative("reduce", 2, &reduce);
}