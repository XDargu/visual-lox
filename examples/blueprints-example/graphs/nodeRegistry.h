
# pragma once

#include "node.h"
#include "idgeneration.h"

#include <Object.h>
#include <Vm.h>

enum class NativeFunctionFlags
{
    Implicit = 1 << 0
};

// TODO: Move to some macro?
constexpr inline NativeFunctionFlags operator~ (NativeFunctionFlags a) { return (NativeFunctionFlags)~(int)a; }
constexpr inline NativeFunctionFlags operator| (NativeFunctionFlags a, NativeFunctionFlags b) { return (NativeFunctionFlags)((int)a | (int)b); }
constexpr inline NativeFunctionFlags operator& (NativeFunctionFlags a, NativeFunctionFlags b) { return (NativeFunctionFlags)((int)a & (int)b); }
constexpr inline NativeFunctionFlags operator^ (NativeFunctionFlags a, NativeFunctionFlags b) { return (NativeFunctionFlags)((int)a ^ (int)b); }
constexpr inline NativeFunctionFlags& operator|= (NativeFunctionFlags& a, NativeFunctionFlags b) { return (NativeFunctionFlags&)((int&)a |= (int)b); }
constexpr inline NativeFunctionFlags& operator&= (NativeFunctionFlags& a, NativeFunctionFlags b) { return (NativeFunctionFlags&)((int&)a &= (int)b); }
constexpr inline NativeFunctionFlags& operator^= (NativeFunctionFlags& a, NativeFunctionFlags b) { return (NativeFunctionFlags&)((int&)a ^= (int)b); }

constexpr inline bool HasFlag(NativeFunctionFlags a, NativeFunctionFlags b) { return (int)(a & b) != 0; }

struct NativeFunctionDef
{
    struct Input
    {
        std::string name;
        Value value;
    };

    std::vector<Input> inputs;
    std::vector<Input> outputs;

    NativeFunctionFlags flags;

    std::string name;
    NativeFn function;

    NodePtr MakeNode(IDGenerator& IDGenerator);
};

class NodeRegistry
{
public:
    void RegisterDefinitions();
    void RegisterNativeFunc(const char* name, std::vector<NativeFunctionDef::Input>&& inputs, std::vector<NativeFunctionDef::Input>&& outputs, NativeFn fun, NativeFunctionFlags flags);
    void RegisterNatives(VM& vm);

    std::vector<NativeFunctionDef> definitions;
};