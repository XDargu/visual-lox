
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
    GetVariableNode(int id, const char* name, const ScriptPropertyPtr& pProperty, ScriptElementID varID)
        : Node(id, name, ImColor(255, 128, 128))
        , pPropertyDef(pProperty)
    {
        refId = varID;
        Category = NodeCategory::Variable;
        Type = NodeType::SimpleGet;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        // Variables are loaded directly when compiling inputs
    }

    void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        Flags = ClearFlag(Flags, NodeFlags::Error);

        RefreshDefinition(script);

        if (!pPropertyDef)
        {
            Flags |= NodeFlags::Error;
            Error = "Missing variable with ID: " + std::to_string(refId);
            return;
        }

        Outputs[0].Name = pPropertyDef->Name;
        Outputs[0].Type = TypeOfValue(pPropertyDef->defaultValue);
    }

    void RefreshDefinition(const Script& script)
    {
        pPropertyDef = ScriptUtils::FindVariableById(script, refId);
    }

    ScriptPropertyPtr pPropertyDef;
};

static NodePtr BuildGetVariableNode(IDGenerator& IDGenerator, const ScriptPropertyPtr& pProperty)
{
    NodePtr node = std::make_shared<GetVariableNode>(IDGenerator.GetNextId(), "", pProperty, pProperty->ID);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), pProperty->Name.c_str(), TypeOfValue(pProperty->defaultValue));

    return node;
}

struct SetVariableNode : public Node
{
    SetVariableNode(int id, const char* name, const ScriptPropertyPtr& pProperty, ScriptElementID varID)
        : Node(id, name, ImColor(255, 128, 128))
        , pPropertyDef(pProperty)
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
        }
    }

    void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        Flags = ClearFlag(Flags, NodeFlags::Error);

        RefreshDefinition(script);

        if (!pPropertyDef)
        {
            Flags |= NodeFlags::Error;
            Error = "Missing variable with ID: " + std::to_string(refId);
            return;
        }

        Inputs[1].Name = pPropertyDef->Name;
        if (TypeOfValue(pPropertyDef->defaultValue) != Inputs[1].Type)
        {
            Inputs[1].Type = TypeOfValue(pPropertyDef->defaultValue);
            InputValues[1] = pPropertyDef->defaultValue;
        }
    }

    void RefreshDefinition(const Script& script)
    {
        pPropertyDef = ScriptUtils::FindVariableById(script, refId);
    }

    ScriptPropertyPtr pPropertyDef;
};

static NodePtr BuildSetVariableNode(IDGenerator& IDGenerator, const ScriptPropertyPtr& pProperty)
{
    NodePtr node = std::make_shared<SetVariableNode>(IDGenerator.GetNextId(), "Set", pProperty, pProperty->ID);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), pProperty->Name.c_str(), TypeOfValue(pProperty->defaultValue));

    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);

    node->InputValues.push_back(Value());
    node->InputValues.push_back(Value(MakeValueFromType(TypeOfValue(pProperty->defaultValue))));

    return node;
}