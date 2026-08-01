#pragma once

#include "../../graphs/graphCompiler.h"
#include "../../graphs/idgeneration.h"
#include "../../script/script.h"
#include "../../utilities/utils.h"

#include <Compiler.h>

#include <algorithm>

namespace ObjectNodeUtils
{
inline void EmitNamedOperation(Compiler& compiler, OpCode shortOp, OpCode longOp,
                               const std::string& name)
{
    const Token token(TokenType::IDENTIFIER, name.c_str(), name.length(), 0);
    compiler.emitOpWithValue(shortOp, longOp, compiler.identifierConstant(token));
}

inline void CompileReceiverInput(CompilerContext& context, const Graph& graph,
                                 const Node& node, const Pin& input,
                                 const Value& value)
{
    if (context.script &&
        GraphUtils::UsesImplicitReceiver(
            *context.script, context.functionId, graph, node))
    {
        static constexpr char name[] = "this";
        context.compiler.namedVariable(
            Token(TokenType::THIS, name, 4, 0), false);
        return;
    }
    GraphCompiler::CompileInput(context, graph, input, value);
}

inline void RefreshCallInputs(Node& node, IDGenerator& ids,
                              const std::vector<BasicFunctionDef::Input>& parameters,
                              int fixedInputs)
{
    std::vector<std::pair<Pin, Value>> saved;
    for (size_t index = static_cast<size_t>(fixedInputs); index < node.Inputs.size(); ++index)
        saved.emplace_back(node.Inputs[index], node.InputValues[index]);
    for (size_t index = 0; index < node.UnresolvedInputs.size(); ++index)
        saved.emplace_back(node.UnresolvedInputs[index], node.UnresolvedInputValues[index]);

    node.Inputs.erase(node.Inputs.begin() + fixedInputs, node.Inputs.end());
    node.InputValues.erase(node.InputValues.begin() + fixedInputs, node.InputValues.end());
    for (const BasicFunctionDef::Input& parameter : parameters)
    {
        const auto existing = std::find_if(saved.begin(), saved.end(), [&](const auto& item)
        {
            return item.first.Identity.kind == PortIdentityKind::Script
                ? PortIdentitiesMatch(item.first.Identity, PortIdentity::Script(parameter.id, parameter.persistentId)) : item.first.Name == parameter.name;
        });
        Pin input = existing != saved.end() ? existing->first : Pin(ids.GetNextId(), parameter.name.c_str(), parameter.type, parameter.description);
        Value value = existing != saved.end() ? existing->second : parameter.value;
        if (existing != saved.end()) saved.erase(existing);
        input.Name = parameter.name;
        input.Type = input.DeclaredType = parameter.type;
        input.Description = parameter.description;
        input.Identity = PortIdentity::Script(parameter.id, parameter.persistentId);
        node.Inputs.push_back(std::move(input));
        node.InputValues.push_back(value);
    }

    node.UnresolvedInputs.clear();
    node.UnresolvedInputValues.clear();
    for (auto& [pin, value] : saved)
    {
        node.UnresolvedInputs.push_back(std::move(pin));
        node.UnresolvedInputValues.push_back(value);
    }
}

inline void RefreshCallOutputs(Node& node, IDGenerator& ids,
                               const std::vector<BasicFunctionDef::Input>& results,
                               int fixedOutputs)
{
    std::vector<Pin> saved;
    for (size_t index = static_cast<size_t>(fixedOutputs); index < node.Outputs.size(); ++index)
        saved.push_back(node.Outputs[index]);
    saved.insert(saved.end(), node.UnresolvedOutputs.begin(), node.UnresolvedOutputs.end());

    std::vector<Pin> refreshed;
    refreshed.reserve(static_cast<size_t>(fixedOutputs) + results.size());
    for (int i = 0; i < fixedOutputs && i < static_cast<int>(node.Outputs.size()); ++i)
        refreshed.push_back(node.Outputs[i]);

    for (const BasicFunctionDef::Input& result : results)
    {
        const auto existing = std::find_if(saved.begin(), saved.end(), [&](const Pin& output)
            {
                return output.Identity.kind == PortIdentityKind::Script
                    ? PortIdentitiesMatch(output.Identity, PortIdentity::Script(result.id, result.persistentId)) : output.Name == result.name;
            });
        if (existing != saved.end())
        {
            Pin output = *existing;
            saved.erase(existing);
            output.Type = output.DeclaredType = result.type;
            output.Description = result.description;
            output.Name = result.name;
            output.Identity = PortIdentity::Script(result.id, result.persistentId);
            refreshed.push_back(std::move(output));
        }
        else
        {
            refreshed.emplace_back(ids.GetNextId(), result.name.c_str(), result.type, result.description);
            refreshed.back().Identity = PortIdentity::Script(result.id, result.persistentId);
        }
    }
    node.Outputs = std::move(refreshed);
    node.UnresolvedOutputs = std::move(saved);
}

inline TypeRef FunctionType(const BasicFunctionDef& definition)
{
    std::vector<TypeRef> inputs;
    std::vector<TypeRef> outputs;
    inputs.reserve(definition.inputs.size());
    outputs.reserve(definition.outputs.size());
    for (const BasicFunctionDef::Input& input : definition.inputs)
        inputs.push_back(input.type);
    for (const BasicFunctionDef::Input& output : definition.outputs)
        outputs.push_back(output.type);
    return TypeRef::Function(std::move(inputs), std::move(outputs));
}
}

struct ConstructObjectNode : public Node
{
    ConstructObjectNode(int id, const ScriptClassPtr& scriptClass, ScriptElementID classId)
        : Node(id, scriptClass ? scriptClass->Name.c_str() : "Missing Class", ImColor(51, 150, 215))
        , classDefinition(scriptClass)
    {
        Category = NodeCategory::Function;
        refId = classId;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::BeginInputs || !classDefinition)
            return;
        Compiler& compiler = context.compiler;
        const Token token(TokenType::IDENTIFIER, classDefinition->Name.c_str(),
                          classDefinition->Name.length(), 0);
        compiler.namedVariable(token, false);
        for (size_t i = 1; i < Inputs.size(); ++i)
            GraphCompiler::CompileInput(context, graph, Inputs[i], InputValues[i]);
        compiler.emitBytes(OpByte(OpCode::OP_CALL), static_cast<uint8_t>(Inputs.size() - 1));
        GraphCompiler::CompileOutput(context, graph, Outputs[1]);
    }

    void Refresh(const Script& script, IDGenerator& ids) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);
        classDefinition = ScriptUtils::FindClassById(script, refId);
        if (!classDefinition)
        {
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing class with ID: " + std::to_string(refId.id);
            return;
        }
        Name = classDefinition->Name;
        Description = "Constructs an instance of '" + classDefinition->Name + "'.";
        Outputs[1].Type = Outputs[1].DeclaredType =
            TypeRef::Object(classDefinition->ID.id, classDefinition->Name);
        static const std::vector<BasicFunctionDef::Input> noInputs;
        const auto& inputs = classDefinition->constructor
            ? classDefinition->constructor->functionDef->inputs : noInputs;
        ObjectNodeUtils::RefreshCallInputs(*this, ids, inputs, 1);
    }

    ScriptClassPtr classDefinition;
};

inline NodePtr BuildConstructObjectNode(IDGenerator& ids, const ScriptClassPtr& scriptClass,
                                        ScriptElementID classId = ScriptElementID::Invalid)
{
    if (scriptClass) classId = scriptClass->ID;
    NodePtr node = std::make_shared<ConstructObjectNode>(ids.GetNextId(), scriptClass, classId);
    node->SerializationType = "class.construct";
    node->DefinitionId = "vlox.script.class.construct";
    node->Description = scriptClass
        ? "Constructs an instance of '" + scriptClass->Name + "'."
        : "Constructs an object instance.";
    node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow,
        "Executes the constructor.");
    node->Inputs.back().Identity = PortIdentity::Fixed("execute");
    node->InputValues.emplace_back(Value());
    if (scriptClass && scriptClass->constructor)
        for (const auto& input : scriptClass->constructor->functionDef->inputs)
        {
            node->Inputs.emplace_back(ids.GetNextId(), input.name.c_str(), input.type,
                input.description);
            node->Inputs.back().Identity = PortIdentity::Script(input.id, input.persistentId);
            node->InputValues.emplace_back(input.value);
        }
    node->Outputs.emplace_back(ids.GetNextId(), "", PinType::Flow,
        "Continues after construction.");
    node->Outputs.back().Identity = PortIdentity::Fixed("then");
    node->Outputs.emplace_back(ids.GetNextId(), "Instance",
        scriptClass ? TypeRef::Object(scriptClass->ID.id, scriptClass->Name)
                    : TypeRef(PinType::Object),
        "The newly constructed instance.");
    node->Outputs.back().Identity = PortIdentity::Fixed("instance");
    return node;
}

struct ThisNode : public Node
{
    explicit ThisNode(int id) : Node(id, "This", ImColor(51, 150, 215))
    {
        Category = NodeCategory::Variable;
        Type = NodeType::SimpleGet;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::PullOutput)
            return;
        static constexpr char name[] = "this";
        context.compiler.namedVariable(Token(TokenType::THIS, name, 4, 0), false);
        GraphCompiler::CompileOutput(context, graph, Outputs[0]);
    }
};

inline NodePtr BuildThisNode(IDGenerator& ids,
                            TypeRef thisType = TypeRef(PinType::Object))
{
    NodePtr node = std::make_shared<ThisNode>(ids.GetNextId());
    node->SerializationType = "class.this";
    node->DefinitionId = "vlox.script.class.this";
    node->Description = "Gets the current class instance.";
    node->Outputs.emplace_back(ids.GetNextId(), "This", std::move(thisType),
        "The current class instance.");
    return node;
}

struct GetPropertyNode : public Node
{
    GetPropertyNode(int id, const ScriptPropertyPtr& property, ScriptElementID propertyId)
        : Node(id, property ? property->Name.c_str() : "Missing Property", ImColor(51, 150, 215))
        , propertyDefinition(property)
    {
        Category = NodeCategory::Variable;
        Type = NodeType::SimpleGet;
        ShowOutputPinNames = false;
        refId = propertyId;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::PullOutput || !propertyDefinition)
            return;
        ObjectNodeUtils::CompileReceiverInput(
            context, graph, *this, Inputs[0], InputValues[0]);
        ObjectNodeUtils::EmitNamedOperation(context.compiler, OpCode::OP_GET_PROPERTY,
            OpCode::OP_GET_PROPERTY_LONG, propertyDefinition->Name);
        GraphCompiler::CompileOutput(context, graph, Outputs[0]);
    }

    int GetReceiverInputIndex() const override { return 0; }

    void Refresh(const Script& script, IDGenerator&) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);
        propertyDefinition = ScriptUtils::FindClassPropertyById(script, refId);
        if (!propertyDefinition)
        {
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing class property with ID: " + std::to_string(refId.id);
            return;
        }
        Name = Outputs[0].Name = propertyDefinition->Name;
        Description = "Gets property '" + propertyDefinition->Name + "'. " +
            propertyDefinition->Description;
        if (const ScriptClassPtr owner = ScriptUtils::FindOwningClass(script, refId.id))
            Inputs[0].Type = Inputs[0].DeclaredType =
                TypeRef::Object(owner->ID.id, owner->Name);
        Outputs[0].Type = Outputs[0].DeclaredType = propertyDefinition->type;
        Outputs[0].Description = propertyDefinition->Description;
    }

    ScriptPropertyPtr propertyDefinition;
};

inline NodePtr BuildGetPropertyNode(IDGenerator& ids, const ScriptPropertyPtr& property,
                                    ScriptElementID propertyId = ScriptElementID::Invalid,
                                    TypeRef instanceType = TypeRef(PinType::Object))
{
    if (property) propertyId = property->ID;
    NodePtr node = std::make_shared<GetPropertyNode>(ids.GetNextId(), property, propertyId);
    node->SerializationType = "property.get";
    node->DefinitionId = "vlox.script.property.get";
    node->Description = property
        ? "Gets property '" + property->Name + "'. " + property->Description
        : "Gets an object property.";
    node->Inputs.emplace_back(ids.GetNextId(), "Instance", std::move(instanceType),
        "The instance that owns the property.");
    node->InputValues.emplace_back(Value());
    node->Outputs.emplace_back(ids.GetNextId(), property ? property->Name.c_str() : "Value",
        property ? property->type : TypeRef(PinType::Any),
        property ? property->Description : std::string{});
    return node;
}

struct SetPropertyNode : public Node
{
    SetPropertyNode(int id, const ScriptPropertyPtr& property, ScriptElementID propertyId)
        : Node(id, property ? ("Set " + property->Name).c_str() : "Missing Property", ImColor(51, 150, 215))
        , propertyDefinition(property)
    {
        Category = NodeCategory::Variable;
        refId = propertyId;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::BeginInputs || !propertyDefinition)
            return;
        ObjectNodeUtils::CompileReceiverInput(
            context, graph, *this, Inputs[1], InputValues[1]);
        GraphCompiler::CompileInput(context, graph, Inputs[2], InputValues[2]);
        ObjectNodeUtils::EmitNamedOperation(context.compiler, OpCode::OP_SET_PROPERTY,
            OpCode::OP_SET_PROPERTY_LONG, propertyDefinition->Name);
        GraphCompiler::CompileOutput(context, graph, Outputs[1]);
    }

    int GetReceiverInputIndex() const override { return 1; }

    void Refresh(const Script& script, IDGenerator&) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);
        propertyDefinition = ScriptUtils::FindClassPropertyById(script, refId);
        if (!propertyDefinition)
        {
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing class property with ID: " + std::to_string(refId.id);
            return;
        }
        Name = "Set " + propertyDefinition->Name;
        Description = "Sets property '" + propertyDefinition->Name + "'. " +
            propertyDefinition->Description;
        if (const ScriptClassPtr owner = ScriptUtils::FindOwningClass(script, refId.id))
            Inputs[1].Type = Inputs[1].DeclaredType =
                TypeRef::Object(owner->ID.id, owner->Name);
        Inputs[2].Name = Outputs[1].Name = propertyDefinition->Name;
        Inputs[2].Description = Outputs[1].Description =
            propertyDefinition->Description;
        Inputs[2].Type = Inputs[2].DeclaredType = propertyDefinition->type;
        Outputs[1].Type = Outputs[1].DeclaredType = propertyDefinition->type;
        InputValues[2] = propertyDefinition->defaultValue;
    }

    ScriptPropertyPtr propertyDefinition;
};

inline NodePtr BuildSetPropertyNode(IDGenerator& ids, const ScriptPropertyPtr& property,
                                    ScriptElementID propertyId = ScriptElementID::Invalid,
                                    TypeRef instanceType = TypeRef(PinType::Object))
{
    if (property) propertyId = property->ID;
    NodePtr node = std::make_shared<SetPropertyNode>(ids.GetNextId(), property, propertyId);
    node->SerializationType = "property.set";
    node->DefinitionId = "vlox.script.property.set";
    node->Description = property
        ? "Sets property '" + property->Name + "'. " + property->Description
        : "Sets an object property.";
    node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow,
        "Executes the assignment.");
    node->Inputs.emplace_back(ids.GetNextId(), "Instance", std::move(instanceType),
        "The instance that owns the property.");
    node->Inputs.emplace_back(ids.GetNextId(), property ? property->Name.c_str() : "Value",
        property ? property->type : TypeRef(PinType::Any),
        property ? property->Description : std::string{});
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(property ? property->defaultValue : Value());
    node->Outputs.emplace_back(ids.GetNextId(), "", PinType::Flow,
        "Continues after the assignment.");
    node->Outputs.emplace_back(ids.GetNextId(), property ? property->Name.c_str() : "Value",
        property ? property->type : TypeRef(PinType::Any),
        property ? property->Description : std::string{});
    return node;
}

struct GetMethodNode : public Node
{
    GetMethodNode(int id, const ScriptFunctionPtr& method, ScriptElementID methodId)
        : Node(id, method ? ("Get " + method->functionDef->name).c_str()
                          : "Missing Method",
               ImColor(255, 128, 128))
        , methodDefinition(method)
    {
        Category = NodeCategory::Function;
        Type = NodeType::SimpleGet;
        ShowOutputPinNames = false;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
        refId = methodId;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::PullOutput || !methodDefinition)
            return;
        ObjectNodeUtils::CompileReceiverInput(
            context, graph, *this, Inputs[0], InputValues[0]);
        ObjectNodeUtils::EmitNamedOperation(
            context.compiler, OpCode::OP_GET_PROPERTY,
            OpCode::OP_GET_PROPERTY_LONG,
            methodDefinition->functionDef->name);
        GraphCompiler::CompileOutput(context, graph, Outputs[0]);
    }

    int GetReceiverInputIndex() const override { return 0; }

    void Refresh(const Script& script, IDGenerator&) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);
        methodDefinition = ScriptUtils::FindFunctionById(script, refId);
        const ScriptClassPtr owner =
            ScriptUtils::FindOwningClass(script, refId.id);
        if (!methodDefinition || !owner)
        {
            methodDefinition = nullptr;
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing class method with ID: " +
                std::to_string(refId.id);
            return;
        }

        const std::string& methodName = methodDefinition->functionDef->name;
        Name = "Get " + methodName;
        Description = "Gets method '" + methodName +
            "' from a specific " + owner->Name + " instance.";
        DefinitionFlags = NodeDefinitionFlags::Pure;
        GenericTypeProperties =
            methodDefinition->functionDef->genericTypeProperties;
        Inputs[0].Type = Inputs[0].DeclaredType =
            TypeRef::Object(owner->ID.id, owner->Name);
        Outputs[0].Name = methodName;
        Outputs[0].Type = Outputs[0].DeclaredType =
            ObjectNodeUtils::FunctionType(*methodDefinition->functionDef);
        Outputs[0].Description = methodDefinition->functionDef->description;
    }

    ScriptFunctionPtr methodDefinition;
};

inline NodePtr BuildGetMethodNode(
    IDGenerator& ids, const ScriptFunctionPtr& method,
    ScriptElementID methodId = ScriptElementID::Invalid,
    TypeRef instanceType = TypeRef(PinType::Object))
{
    if (method) methodId = method->ID;
    NodePtr node =
        std::make_shared<GetMethodNode>(ids.GetNextId(), method, methodId);
    node->SerializationType = "method.get";
    node->DefinitionId = "vlox.script.method.get";
    node->Description = method
        ? "Gets method '" + method->functionDef->name +
            "' from a specific instance."
        : "Gets a method from a specific instance.";
    node->Inputs.emplace_back(
        ids.GetNextId(), "Instance", std::move(instanceType),
        "The instance whose method is returned.");
    node->InputValues.emplace_back(Value());
    node->Outputs.emplace_back(
        ids.GetNextId(), method ? method->functionDef->name.c_str() : "Method",
        method ? ObjectNodeUtils::FunctionType(*method->functionDef)
               : TypeRef(PinType::Function),
        method ? method->functionDef->description : std::string{});
    if (method)
        node->GenericTypeProperties =
            method->functionDef->genericTypeProperties;
    return node;
}

struct MethodCallNode : public Node
{
    MethodCallNode(int id, const ScriptFunctionPtr& method, ScriptElementID methodId)
        : Node(id, method ? method->functionDef->name.c_str() : "Missing Method", ImColor(255, 128, 128))
        , methodDefinition(method)
    {
        Category = NodeCategory::Function;
        refId = methodId;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (!methodDefinition)
            return;
        const bool expressionOnly = GraphUtils::IsNodeImplicit(this);
        if ((stage == CompilationStage::BeginInputs && expressionOnly) ||
            (stage == CompilationStage::PullOutput && !expressionOnly) ||
            (stage != CompilationStage::BeginInputs &&
             stage != CompilationStage::PullOutput))
            return;
        const size_t instanceIndex =
            (HasFlag(DefinitionFlags, NodeDefinitionFlags::ReadOnly) ||
             HasFlag(DefinitionFlags, NodeDefinitionFlags::Pure)) ? 0 : 1;
        ObjectNodeUtils::CompileReceiverInput(
            context, graph, *this, Inputs[instanceIndex], InputValues[instanceIndex]);
        for (size_t i = instanceIndex + 1; i < Inputs.size(); ++i)
            GraphCompiler::CompileInput(context, graph, Inputs[i], InputValues[i]);
        const std::string& name = methodDefinition->functionDef->name;
        const Token token(TokenType::IDENTIFIER, name.c_str(), name.length(), 0);
        context.compiler.emitOpWithValue(OpCode::OP_INVOKE, OpCode::OP_INVOKE_LONG,
                                         context.compiler.identifierConstant(token));
        context.compiler.emitByte(
            static_cast<uint8_t>(Inputs.size() - instanceIndex - 1));
        GraphCompiler::CompileCallResult(
            context, graph, Outputs,
            (HasFlag(DefinitionFlags, NodeDefinitionFlags::ReadOnly) ||
             HasFlag(DefinitionFlags, NodeDefinitionFlags::Pure)) ? 0 : 1);
    }

    int GetReceiverInputIndex() const override
    {
        return (HasFlag(DefinitionFlags, NodeDefinitionFlags::ReadOnly) ||
                HasFlag(DefinitionFlags, NodeDefinitionFlags::Pure)) ? 0 : 1;
    }

    void Refresh(const Script& script, IDGenerator& ids) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);
        methodDefinition = ScriptUtils::FindFunctionById(script, refId);
        const ScriptClassPtr owner = ScriptUtils::FindOwningClass(script, refId.id);
        if (!methodDefinition || !owner)
        {
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing class method with ID: " + std::to_string(refId.id);
            return;
        }
        Name = methodDefinition->functionDef->name;
        Description = methodDefinition->functionDef->description;
        DefinitionFlags = methodDefinition->functionDef->flags;
        GenericTypeProperties =
            methodDefinition->functionDef->genericTypeProperties;
        const bool expressionOnly =
            HasFlag(DefinitionFlags, NodeDefinitionFlags::ReadOnly) ||
            HasFlag(DefinitionFlags, NodeDefinitionFlags::Pure);
        if (expressionOnly)
        {
            for (size_t index = Inputs.size(); index-- > 0;)
            {
                if (Inputs[index].Type != PinType::Flow)
                    continue;
                Inputs.erase(Inputs.begin() + index);
                if (index < InputValues.size())
                    InputValues.erase(InputValues.begin() + index);
            }
            stl::erase_if(Outputs,
                [](const Pin& output) { return output.Type == PinType::Flow; });
        }
        else
        {
            if (std::none_of(Inputs.begin(), Inputs.end(),
                    [](const Pin& input) { return input.Type == PinType::Flow; }))
            {
                Inputs.insert(Inputs.begin(),
                    Pin(ids.GetNextId(), "", PinType::Flow,
                        "Executes this method."));
                Inputs.front().Identity = PortIdentity::Fixed("execute");
                InputValues.insert(InputValues.begin(), Value());
            }
            if (std::none_of(Outputs.begin(), Outputs.end(),
                    [](const Pin& output) { return output.Type == PinType::Flow; }))
            {
                Outputs.insert(Outputs.begin(),
                    Pin(ids.GetNextId(), "", PinType::Flow,
                        "Continues after the method returns."));
                Outputs.front().Identity = PortIdentity::Fixed("then");
            }
        }
        const int instanceIndex = expressionOnly ? 0 : 1;
        Inputs[instanceIndex].Type = Inputs[instanceIndex].DeclaredType =
            TypeRef::Object(owner->ID.id, owner->Name);
        ObjectNodeUtils::RefreshCallInputs(
            *this, ids, methodDefinition->functionDef->inputs,
            instanceIndex + 1);
        ObjectNodeUtils::RefreshCallOutputs(
            *this, ids, methodDefinition->functionDef->outputs,
            expressionOnly ? 0 : 1);
    }

    ScriptFunctionPtr methodDefinition;
};

inline NodePtr BuildMethodCallNode(IDGenerator& ids, const ScriptFunctionPtr& method,
                                   ScriptElementID methodId = ScriptElementID::Invalid,
                                   TypeRef instanceType = TypeRef(PinType::Object))
{
    if (method) methodId = method->ID;
    NodePtr node = std::make_shared<MethodCallNode>(ids.GetNextId(), method, methodId);
    node->SerializationType = "method.call";
    node->DefinitionId = "vlox.script.method.call";
    if (method)
    {
        node->Description = method->functionDef->description;
        node->DefinitionFlags = method->functionDef->flags;
        node->GenericTypeProperties =
            method->functionDef->genericTypeProperties;
    }
    const bool expressionOnly = method &&
        (HasFlag(method->functionDef->flags, NodeDefinitionFlags::ReadOnly) ||
         HasFlag(method->functionDef->flags, NodeDefinitionFlags::Pure));
    if (!expressionOnly)
        node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow,
            "Executes this method.");
    if (!expressionOnly)
        node->Inputs.back().Identity = PortIdentity::Fixed("execute");
    node->Inputs.emplace_back(ids.GetNextId(), "Instance", std::move(instanceType),
        "The instance on which to call the method.");
    node->Inputs.back().Identity = PortIdentity::Fixed("instance");
    if (!expressionOnly)
        node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value());
    if (method) for (const auto& input : method->functionDef->inputs)
    {
        node->Inputs.emplace_back(ids.GetNextId(), input.name.c_str(), input.type,
            input.description);
        node->Inputs.back().Identity = PortIdentity::Script(input.id, input.persistentId);
        node->InputValues.emplace_back(input.value);
    }
    if (!expressionOnly)
        node->Outputs.emplace_back(ids.GetNextId(), "", PinType::Flow,
            "Continues after the method returns.");
    if (!expressionOnly)
        node->Outputs.back().Identity = PortIdentity::Fixed("then");
    if (method)
    {
        for (const auto& output : method->functionDef->outputs)
        {
            node->Outputs.emplace_back(
                ids.GetNextId(), output.name.c_str(), output.type,
                output.description);
            node->Outputs.back().Identity = PortIdentity::Script(output.id, output.persistentId);
        }
    }
    return node;
}
