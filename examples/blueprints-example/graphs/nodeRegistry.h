
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

    bool isImplicit = false;

    std::string name;
    NativeFn function;

    NodePtr MakeNode(IDGenerator& IDGenerator);
};

class NodeRegistry
{
public:
    void RegisterDefinitions();
    void RegisterNatives(VM& vm);

    std::vector<NativeFunctionDef> definitions;
};