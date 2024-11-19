
# pragma once

#include "node.h"
#include "idgeneration.h"

#include <Object.h>
#include <Vm.h>

struct NativeFunctionDef
{
    struct Input
    {
        std::string name;
        Value value;
    };

    std::vector<Input> inputs;
    std::vector<Input> outputs;

    NodeFlags flags;

    std::string name;
    NativeFn function;

    NodePtr MakeNode(IDGenerator& IDGenerator);
};

class NodeRegistry
{
public:
    void RegisterDefinitions();
    void RegisterNativeFunc(const char* name, std::vector<NativeFunctionDef::Input>&& inputs, std::vector<NativeFunctionDef::Input>&& outputs, NativeFn fun, NodeFlags flags);
    void RegisterNatives(VM& vm);

    std::vector<NativeFunctionDef> definitions;
};