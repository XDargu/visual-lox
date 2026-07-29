#pragma once

#include "../../examples/blueprints-example/apps/visualApplication.h"
#include "../../examples/blueprints-example/graphs/idgeneration.h"
#include "../../examples/blueprints-example/graphs/nodeRegistry.h"
#include "../../examples/blueprints-example/native/nodes/function.h"
#include "../../examples/blueprints-example/native/nodes/variable.h"
#include "../../examples/blueprints-example/script/script.h"

#include <Object.h>
#include <Vm.h>

#include <filesystem>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace ExampleGenerator
{

inline Value StringValue(const char* text)
{
    return Value(copyString(text, static_cast<int>(std::char_traits<char>::length(text))));
}

struct Builder
{
    explicit Builder(const NodeRegistry& registry)
        : registry(registry)
    {
        script.ID = ids.GetNextId();
        script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");
    }

    ScriptPropertyPtr Number(const char* name, double value)
    {
        return Property(name, PinType::Float, Value(value));
    }

    ScriptPropertyPtr Boolean(const char* name, bool value)
    {
        return Property(name, PinType::Bool, Value(value));
    }

    ScriptPropertyPtr NumberList(const char* name)
    {
        return Property(name, TypeRef::List(PinType::Float), Value(newList()));
    }

    ScriptPropertyPtr LocalNumber(const ScriptFunctionPtr& function, const char* name, double value)
    {
        ScriptPropertyPtr property = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
        property->type = PinType::Float;
        property->defaultValue = Value(value);
        function->variables.push_back(property);
        return property;
    }

    ScriptFunctionPtr Function(const char* name, const char* description, bool pure = false)
    {
        ScriptFunctionPtr function = std::make_shared<ScriptFunction>(ids.GetNextId(), name);
        function->functionDef->description = description;
        if (pure)
            function->functionDef->flags |= NodeDefinitionFlags::Pure;
        script.functions.push_back(function);
        return function;
    }

    NodePtr Compiled(const char* name)
    {
        const CompiledNodeDefPtr definition = registry.FindCompiled(name);
        if (!definition)
            throw std::runtime_error(std::string("Missing compiled definition: ") + name);
        return definition->MakeNode(ids);
    }

    NodePtr Native(const char* name)
    {
        const NativeFunctionDef* definition = registry.FindNative(name);
        if (!definition)
            throw std::runtime_error(std::string("Missing native definition: ") + name);
        return definition->functionDef->MakeNode(ids, ScriptElementID::Invalid);
    }

    NodePtr Call(const ScriptFunctionPtr& function)
    {
        return function->functionDef->MakeNode(ids, function->ID);
    }

    NodePtr Get(const ScriptPropertyPtr& property, const ScriptFunctionPtr& owner = nullptr)
    {
        return BuildGetVariableNode(ids, property, ScriptElementID::Invalid, owner ? owner->ID : ScriptElementID::Invalid);
    }

    NodePtr Set(const ScriptPropertyPtr& property, const ScriptFunctionPtr& owner = nullptr)
    {
        return BuildSetVariableNode(ids, property, ScriptElementID::Invalid, owner ? owner->ID : ScriptElementID::Invalid);
    }

    void Add(Graph& graph, const NodePtr& node)
    {
        NodeUtils::BuildNode(node);
        graph.AddNode(node);
    }

    void Add(Graph& graph, std::initializer_list<NodePtr> nodes)
    {
        for (const NodePtr& node : nodes)
            Add(graph, node);
    }

    void Link(Graph& graph, const Pin& output, const Pin& input)
    {
        graph.AddLink(::Link(ids.GetNextId(), output.ID, input.ID));
    }

    Pin& Input(const NodePtr& node, const char* name)
    {
        Pin* pin = node->FindInputByName(name);
        if (!pin)
            throw std::runtime_error("Missing input '" + std::string(name) + "' on " + node->Name);
        return *pin;
    }

    Pin& Output(const NodePtr& node, const char* name)
    {
        Pin* pin = node->FindOutputByName(name);
        if (!pin)
            throw std::runtime_error("Missing output '" + std::string(name) + "' on " + node->Name);
        return *pin;
    }

    void Default(const NodePtr& node, const char* name, Value value)
    {
        Pin& pin = Input(node, name);
        for (size_t index = 0; index < node->Inputs.size(); ++index)
        {
            if (node->Inputs[index].ID != pin.ID)
                continue;
            node->InputValues[index] = value;
            return;
        }
    }

    const NodeRegistry& registry;
    IDGenerator ids;
    Script script;

private:
    ScriptPropertyPtr Property(const char* name, TypeRef type, Value value)
    {
        ScriptPropertyPtr property = std::make_shared<ScriptProperty>(ids.GetNextId(), name);
        property->type = std::move(type);
        property->defaultValue = value;
        script.variables.push_back(property);
        return property;
    }
};

void ValidateCompileAndSave(VM& vm, const NodeRegistry& registry, Script& script, const std::filesystem::path& output);

Script MakeRockPaperScissors(const NodeRegistry& registry);
Script MakeGameOfLife(const NodeRegistry& registry);
Script MakeMandelbrot(const NodeRegistry& registry);

void SmokeTestGameOfLife(VM& vm, const NodeRegistry& registry, const std::filesystem::path& path);
void SmokeTestMandelbrot(VM& vm, const NodeRegistry& registry, const std::filesystem::path& path);

}
