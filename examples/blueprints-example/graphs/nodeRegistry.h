
# pragma once

#include "node.h"
#include "idgeneration.h"
#include "../shared/functionShared.h"

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

struct NativeFunctionDef
{
    BasicFunctionDefPtr functionDef;
    NativeFn nativeFun;
};

class NodeRegistry
{
public:
    void RegisterDefinitions();
    void RegisterNativeFunc(const char* name, std::vector<BasicFunctionDef::Input>&& inputs, std::vector<BasicFunctionDef::Input>&& outputs, NativeFn fun, NodeFlags flags);
    void RegisterNativeFunc(const char* name, std::vector<BasicFunctionDef::Input>&& outputs, NativeFn fun, NodeFlags flags, BasicFunctionDef::DynamicInputProps&& dynamicProps);
    void RegisterNatives(VM& vm);
    void RegisterCompiledNode(const char* name, NodeCreationFun creationFunc);

    std::vector<NativeFunctionDef> nativeDefinitions;
    std::vector<CompiledNodeDefPtr> compiledDefinitions;
};