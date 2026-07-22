#pragma once

#include "../../graphs/graphCompiler.h"
#include "../../graphs/idgeneration.h"
#include "../../script/script.h"

#include <Compiler.h>

namespace ObjectNodeUtils
{
inline void EmitNamedOperation(Compiler& compiler, OpCode shortOp, OpCode longOp,
                               const std::string& name)
{
    const Token token(TokenType::IDENTIFIER, name.c_str(), name.length(), 0);
    compiler.emitOpWithValue(shortOp, longOp, compiler.identifierConstant(token));
}

inline void RefreshCallInputs(Node& node, IDGenerator& ids,
                              const std::vector<BasicFunctionDef::Input>& parameters,
                              int fixedInputs)
{
    for (size_t i = 0; i < parameters.size(); ++i)
    {
        const BasicFunctionDef::Input& parameter = parameters[i];
        const size_t index = static_cast<size_t>(fixedInputs) + i;
        if (index < node.Inputs.size())
        {
            node.Inputs[index].Name = parameter.name;
            node.Inputs[index].Type = TypeOfValue(parameter.value);
            node.InputValues[index] = parameter.value;
        }
        else
        {
            node.Inputs.emplace_back(ids.GetNextId(), parameter.name.c_str(), TypeOfValue(parameter.value));
            node.InputValues.emplace_back(parameter.value);
        }
    }
    const size_t targetSize = static_cast<size_t>(fixedInputs) + parameters.size();
    if (node.Inputs.size() > targetSize)
        node.Inputs.erase(node.Inputs.begin() + targetSize, node.Inputs.end());
    node.InputValues.resize(node.Inputs.size());
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
    node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow);
    node->InputValues.emplace_back(Value());
    if (scriptClass && scriptClass->constructor)
        for (const auto& input : scriptClass->constructor->functionDef->inputs)
        {
            node->Inputs.emplace_back(ids.GetNextId(), input.name.c_str(), TypeOfValue(input.value));
            node->InputValues.emplace_back(input.value);
        }
    node->Outputs.emplace_back(ids.GetNextId(), "", PinType::Flow);
    node->Outputs.emplace_back(ids.GetNextId(), "Instance", PinType::Object);
    return node;
}

struct ThisNode : public Node
{
    explicit ThisNode(int id) : Node(id, "This", ImColor(51, 150, 215))
    {
        Category = NodeCategory::Variable;
        Type = NodeType::SimpleGet;
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

inline NodePtr BuildThisNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<ThisNode>(ids.GetNextId());
    node->SerializationType = "class.this";
    node->Outputs.emplace_back(ids.GetNextId(), "This", PinType::Object);
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
        refId = propertyId;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::PullOutput || !propertyDefinition)
            return;
        GraphCompiler::CompileInput(context, graph, Inputs[0], InputValues[0]);
        ObjectNodeUtils::EmitNamedOperation(context.compiler, OpCode::OP_GET_PROPERTY,
            OpCode::OP_GET_PROPERTY_LONG, propertyDefinition->Name);
        GraphCompiler::CompileOutput(context, graph, Outputs[0]);
    }

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
        Outputs[0].Type = TypeOfValue(propertyDefinition->defaultValue);
    }

    ScriptPropertyPtr propertyDefinition;
};

inline NodePtr BuildGetPropertyNode(IDGenerator& ids, const ScriptPropertyPtr& property,
                                    ScriptElementID propertyId = ScriptElementID::Invalid)
{
    if (property) propertyId = property->ID;
    NodePtr node = std::make_shared<GetPropertyNode>(ids.GetNextId(), property, propertyId);
    node->SerializationType = "property.get";
    node->Inputs.emplace_back(ids.GetNextId(), "Instance", PinType::Object);
    node->InputValues.emplace_back(Value());
    node->Outputs.emplace_back(ids.GetNextId(), property ? property->Name.c_str() : "Value",
        property ? TypeOfValue(property->defaultValue) : PinType::Any);
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
        GraphCompiler::CompileInput(context, graph, Inputs[1], InputValues[1]);
        GraphCompiler::CompileInput(context, graph, Inputs[2], InputValues[2]);
        ObjectNodeUtils::EmitNamedOperation(context.compiler, OpCode::OP_SET_PROPERTY,
            OpCode::OP_SET_PROPERTY_LONG, propertyDefinition->Name);
        GraphCompiler::CompileOutput(context, graph, Outputs[1]);
    }

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
        Inputs[2].Name = Outputs[1].Name = propertyDefinition->Name;
        Inputs[2].Type = Outputs[1].Type = TypeOfValue(propertyDefinition->defaultValue);
        InputValues[2] = propertyDefinition->defaultValue;
    }

    ScriptPropertyPtr propertyDefinition;
};

inline NodePtr BuildSetPropertyNode(IDGenerator& ids, const ScriptPropertyPtr& property,
                                    ScriptElementID propertyId = ScriptElementID::Invalid)
{
    if (property) propertyId = property->ID;
    NodePtr node = std::make_shared<SetPropertyNode>(ids.GetNextId(), property, propertyId);
    node->SerializationType = "property.set";
    node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(ids.GetNextId(), "Instance", PinType::Object);
    node->Inputs.emplace_back(ids.GetNextId(), property ? property->Name.c_str() : "Value",
        property ? TypeOfValue(property->defaultValue) : PinType::Any);
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(property ? property->defaultValue : Value());
    node->Outputs.emplace_back(ids.GetNextId(), "", PinType::Flow);
    node->Outputs.emplace_back(ids.GetNextId(), property ? property->Name.c_str() : "Value",
        property ? TypeOfValue(property->defaultValue) : PinType::Any);
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
        if (stage != CompilationStage::BeginInputs || !methodDefinition)
            return;
        GraphCompiler::CompileInput(context, graph, Inputs[1], InputValues[1]);
        for (size_t i = 2; i < Inputs.size(); ++i)
            GraphCompiler::CompileInput(context, graph, Inputs[i], InputValues[i]);
        const std::string& name = methodDefinition->functionDef->name;
        const Token token(TokenType::IDENTIFIER, name.c_str(), name.length(), 0);
        context.compiler.emitOpWithValue(OpCode::OP_INVOKE, OpCode::OP_INVOKE_LONG,
                                         context.compiler.identifierConstant(token));
        context.compiler.emitByte(static_cast<uint8_t>(Inputs.size() - 2));
        if (Outputs.size() > 1)
            GraphCompiler::CompileOutput(context, graph, Outputs[1]);
        else
            context.compiler.emitByte(OpByte(OpCode::OP_POP));
    }

    void Refresh(const Script& script, IDGenerator& ids) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);
        methodDefinition = ScriptUtils::FindFunctionById(script, refId);
        if (!methodDefinition || !ScriptUtils::FindOwningClass(script, refId))
        {
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing class method with ID: " + std::to_string(refId.id);
            return;
        }
        Name = methodDefinition->functionDef->name;
        ObjectNodeUtils::RefreshCallInputs(*this, ids, methodDefinition->functionDef->inputs, 2);
        if (!methodDefinition->functionDef->outputs.empty())
        {
            const auto& output = methodDefinition->functionDef->outputs.front();
            if (Outputs.size() == 1)
                Outputs.emplace_back(ids.GetNextId(), output.name.c_str(), TypeOfValue(output.value));
            else
            {
                Outputs[1].Name = output.name;
                Outputs[1].Type = TypeOfValue(output.value);
            }
        }
        else if (Outputs.size() > 1)
            Outputs.erase(Outputs.begin() + 1, Outputs.end());
    }

    ScriptFunctionPtr methodDefinition;
};

inline NodePtr BuildMethodCallNode(IDGenerator& ids, const ScriptFunctionPtr& method,
                                   ScriptElementID methodId = ScriptElementID::Invalid)
{
    if (method) methodId = method->ID;
    NodePtr node = std::make_shared<MethodCallNode>(ids.GetNextId(), method, methodId);
    node->SerializationType = "method.call";
    node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(ids.GetNextId(), "Instance", PinType::Object);
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value());
    if (method) for (const auto& input : method->functionDef->inputs)
    {
        node->Inputs.emplace_back(ids.GetNextId(), input.name.c_str(), TypeOfValue(input.value));
        node->InputValues.emplace_back(input.value);
    }
    node->Outputs.emplace_back(ids.GetNextId(), "", PinType::Flow);
    if (method && !method->functionDef->outputs.empty())
    {
        const auto& output = method->functionDef->outputs.front();
        node->Outputs.emplace_back(ids.GetNextId(), output.name.c_str(), TypeOfValue(output.value));
    }
    return node;
}
