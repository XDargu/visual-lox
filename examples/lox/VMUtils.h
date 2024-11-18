#ifndef loxcpp_vmutils_h
#define loxcpp_vmutils_h

#include "Vm.h"

inline bool isFalsey(const Value& value)
{
    return isNil(value) || (isBoolean(value) && !asBoolean(value));
}

inline bool isCallable(const Value& value)
{
    return isClosure(value) || isBoundMethod(value);
}

inline bool isIterable(const Value& value)
{
    return isList(value) || isString(value) || isRange(value);
}

template<typename F>
inline void forEachIterable(const Value& iterable, F predicate)
{
    if (isRange(iterable))
    {
        ObjRange* range = asRange(iterable);
        for (int idx = 0; range->isInBounds(idx); ++idx)
        {
            const Value element(range->getValue(idx));
            if (!predicate(element, idx))
                return;
        }
    }
    else if (isList(iterable))
    {
        ObjList* list = asList(iterable);
        for (int idx = 0; list->isInBounds(idx); ++idx)
        {
            const Value element(list->getValue(idx));
            if (!predicate(element, idx))
                return;
        }
    }
    else if (isString(iterable))
    {
        ObjString* str = asString(iterable);
        for (int idx = 0; idx < str->chars.size(); ++idx)
        {
            const Value element(takeString(&str->chars[idx], 1));
            if (!predicate(element, idx))
                return;
        }
    }
}

inline int pushArgs(VM* vm) { return 0; }

template<typename FirstArg, typename... Args>
inline int pushArgs(VM* vm, const FirstArg& firstValue, const Args&... values)
{
    vm->push(firstValue);
    return pushArgs(vm, values...) + 1;
}

template<typename... Args>
inline Value callFunction(VM* vm, const Value& callable, const Args&... values)
{
    vm->push(callable);
    const int argCount = pushArgs(vm, values...);
    vm->callValue(callable, argCount);
    vm->run(vm->getFrameCount() - 1);
    return vm->pop();
}

#endif
