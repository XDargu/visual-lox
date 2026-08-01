
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include "../../script/property.h"
#include "../../script/script.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct GetVariableNode : public Node
{
    GetVariableNode(int id, const char* name, const ScriptPropertyPtr& pProperty, ScriptElementID varID,
                    ScriptElementID functionID = ScriptElementID::Invalid)
        : Node(id, name, ImColor(255, 128, 128))
        , pPropertyDef(pProperty)
        , ownerFunctionId(functionID)
    {
        refId = varID;
        Category = NodeCategory::Variable;
        Type = NodeType::SimpleGet;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        // Variables are loaded directly when compiling inputs
    }

    void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);

        RefreshDefinition(script);

        if (!pPropertyDef)
        {
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing variable with ID: " + std::to_string(refId);
            return;
        }

        Outputs[0].Name = pPropertyDef->Name;
        Outputs[0].Type = Outputs[0].DeclaredType = pPropertyDef->type;
        Outputs[0].Description = pPropertyDef->Description;
        Description = "Gets variable '" + pPropertyDef->Name + "'. " +
            pPropertyDef->Description;
    }

    void RefreshDefinition(const Script& script)
    {
        pPropertyDef = ownerFunctionId.IsValid()
            ? ScriptUtils::FindVisibleVariableById(script, ownerFunctionId.id, refId.id)
            : ScriptUtils::FindVariableById(script, refId);
    }

    ScriptPropertyPtr pPropertyDef;
    ScriptElementID ownerFunctionId;
};

static NodePtr BuildGetVariableNode(IDGenerator& IDGenerator, const ScriptPropertyPtr& pProperty,
                                    ScriptElementID varID = ScriptElementID::Invalid,
                                    ScriptElementID functionID = ScriptElementID::Invalid)
{
    if (pProperty)
        varID = pProperty->ID;

    NodePtr node = std::make_shared<GetVariableNode>(IDGenerator.GetNextId(), "", pProperty, varID, functionID);
    node->SerializationType = "variable.get";
    node->DefinitionId = "vlox.script.variable.get";
    if (pProperty)
    {
        node->Description = "Gets variable '" + pProperty->Name + "'. " +
            pProperty->Description;
        node->Outputs.emplace_back(IDGenerator.GetNextId(), pProperty->Name.c_str(),
            pProperty->type, pProperty->Description);
    }

    return node;
}

struct SetVariableNode : public Node
{
    SetVariableNode(int id, const char* name, const ScriptPropertyPtr& pProperty, ScriptElementID varID,
                    ScriptElementID functionID = ScriptElementID::Invalid)
        : Node(id, name, ImColor(255, 128, 128))
        , pPropertyDef(pProperty)
        , ownerFunctionId(functionID)
    {
        refId = varID;
        Category = NodeCategory::Variable;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInputs:
        {
            if (!GraphUtils::IsNodeImplicit(this))
                CompileInputs(compilerCtx, graph);
        }
        break;
        case CompilationStage::PullOutput:
        {
            if (GraphUtils::IsNodeImplicit(this))
                CompileInputs(compilerCtx, graph);
        }
        break;
        }
    }

    void CompileInputs(CompilerContext& compilerCtx, const Graph& graph) const
    {
        if (pPropertyDef)
        {
            Compiler& compiler = compilerCtx.compiler;

            GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], InputValues[1]);

            Token varToken(TokenType::VAR, pPropertyDef->Name.c_str(), pPropertyDef->Name.length(), 0);
            compiler.emitVariable(varToken, true);
            compiler.emitByte(OpByte(OpCode::OP_POP));
        }
    }

    void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);

        RefreshDefinition(script);

        if (!pPropertyDef)
        {
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing variable with ID: " + std::to_string(refId);
            return;
        }

        Inputs[1].Name = pPropertyDef->Name;
        Inputs[1].Description = pPropertyDef->Description;
        Description = "Sets variable '" + pPropertyDef->Name + "'. " +
            pPropertyDef->Description;
        if (pPropertyDef->type != Inputs[1].DeclaredType)
        {
            Inputs[1].Type = Inputs[1].DeclaredType = pPropertyDef->type;
        }
        InputValues[1] = pPropertyDef->defaultValue;
    }

    void RefreshDefinition(const Script& script)
    {
        pPropertyDef = ownerFunctionId.IsValid()
            ? ScriptUtils::FindVisibleVariableById(script, ownerFunctionId.id, refId.id)
            : ScriptUtils::FindVariableById(script, refId);
    }

    ScriptPropertyPtr pPropertyDef;
    ScriptElementID ownerFunctionId;
};

static NodePtr BuildSetVariableNode(IDGenerator& IDGenerator, const ScriptPropertyPtr& pProperty,
                                    ScriptElementID varID = ScriptElementID::Invalid,
                                    ScriptElementID functionID = ScriptElementID::Invalid)
{
    if (pProperty)
        varID = pProperty->ID;

    NodePtr node = std::make_shared<SetVariableNode>(IDGenerator.GetNextId(), "Set", pProperty, varID, functionID);
    node->SerializationType = "variable.set";
    node->DefinitionId = "vlox.script.variable.set";
    if (pProperty)
    {
        node->Description = "Sets variable '" + pProperty->Name + "'. " +
            pProperty->Description;
        node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow,
            "Executes the assignment.");
        node->Inputs.emplace_back(IDGenerator.GetNextId(), pProperty->Name.c_str(),
            pProperty->type, pProperty->Description);

        node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow,
            "Continues after the assignment.");

        node->InputValues.push_back(Value());
        node->InputValues.push_back(pProperty->defaultValue);
    }

    return node;
}
