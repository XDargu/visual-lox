
# pragma once

#include "node.h"
#include "idgeneration.h"
#include "../shared/functionShared.h"

#include <Object.h>
#include <Vm.h>

#include <memory>

using NodeCreationFun = NodePtr(*)(IDGenerator& IDGenerator);

std::string ComputeDefinitionCompatibilityFingerprint(const BasicFunctionDef& definition);

struct NodeDocumentation
{
    const char* description;
    std::vector<const char*> inputs;
    std::vector<const char*> outputs;
    const char* dynamicInput = nullptr;
};

struct CompiledNodeDef : public std::enable_shared_from_this<CompiledNodeDef>
{
    NodePtr MakeNode(IDGenerator& IDGenerator);
    
    std::string name;
    std::string id;
    uint32_t revision = 1;
    NodeCreationFun nodeCreationFunc;
    BasicFunctionDefPtr functionDef; // Only used to check the layout
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
    void RegisterNativeFunc(const char* name,
        std::vector<BasicFunctionDef::Input>&& inputs,
        std::vector<BasicFunctionDef::Input>&& outputs, NativeFn fun,
        NodeDefinitionFlags flags, NodeDocumentation documentation,
        std::vector<GenericTypeProperty> genericTypeProperties = {});
    void RegisterNativeFunc(const char* name,
        std::vector<BasicFunctionDef::Input>&& outputs, NativeFn fun,
        NodeDefinitionFlags flags,
        BasicFunctionDef::DynamicInputProps&& dynamicProps,
        NodeDocumentation documentation,
        std::vector<GenericTypeProperty> genericTypeProperties = {});
    void RegisterNatives(VM& vm);
    void RegisterCompiledNode(const char* name, NodeCreationFun creationFunc,
        std::vector<BasicFunctionDef::Input>&& inputs,
        std::vector<BasicFunctionDef::Input>&& outputs,
        NodeDefinitionFlags flags, NodeDocumentation documentation,
        std::vector<GenericTypeProperty> genericTypeProperties = {});
    void RegisterCompiledNode(const char* name, NodeCreationFun creationFunc,
        std::vector<BasicFunctionDef::Input>&& inputs,
        std::vector<BasicFunctionDef::Input>&& outputs,
        NodeDefinitionFlags flags, BasicFunctionDef::DynamicInputProps&& dynamicProps,
        NodeDocumentation documentation,
        std::vector<GenericTypeProperty> genericTypeProperties = {});

    const NativeFunctionDef* FindNative(const std::string& name) const;
    CompiledNodeDefPtr FindCompiled(const std::string& name) const;
    std::vector<NativeFunctionDef> nativeDefinitions;
    std::vector<CompiledNodeDefPtr> compiledDefinitions;

};
