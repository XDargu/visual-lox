
# pragma once

#include "node.h"
#include "idgeneration.h"

#include <Object.h>
#include <Vm.h>

#include <memory>

using NodeCreationFun = NodePtr(*)(IDGenerator& IDGenerator);

struct CompiledNodeDef : public std::enable_shared_from_this<CompiledNodeDef>
{
    NodePtr MakeNode(IDGenerator& IDGenerator);
    
    std::string name;
    NodeCreationFun nodeCreationFunc;
};

using CompiledNodeDefPtr = std::shared_ptr< CompiledNodeDef>;


struct NativeFunctionDef : public std::enable_shared_from_this<NativeFunctionDef>
{
    struct Input
    {
        std::string name;
        Value value;
    };

    struct DynamicInputProps
    {
        int minInputs = 1;
        int maxInputs = 16;
        PinType type = PinType::Any;
        Value defaultValue;
    };

    std::vector<Input> inputs;
    std::vector<Input> outputs;

    NodeFlags flags;

    DynamicInputProps dynamicInputProps;

    std::string name;
    NativeFn function;

    NodePtr MakeNode(IDGenerator& IDGenerator);
};

using NativeFunctionDefPtr = std::shared_ptr< NativeFunctionDef>;

class NodeRegistry
{
public:
    void RegisterDefinitions();
    void RegisterNativeFunc(const char* name, std::vector<NativeFunctionDef::Input>&& inputs, std::vector<NativeFunctionDef::Input>&& outputs, NativeFn fun, NodeFlags flags);
    void RegisterNativeFunc(const char* name, std::vector<NativeFunctionDef::Input>&& outputs, NativeFn fun, NodeFlags flags, NativeFunctionDef::DynamicInputProps&& dynamicProps);
    void RegisterNatives(VM& vm);
    void RegisterCompiledNode(const char* name, NodeCreationFun creationFunc);

    std::vector<NativeFunctionDefPtr> nativeDefinitions;
    std::vector<CompiledNodeDefPtr> compiledDefinitions;
};