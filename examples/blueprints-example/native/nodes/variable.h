
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct GetVariableNode : public Node
{
    GetVariableNode(int id, const char* name, const char* variableName)
        : Node(id, name, ImColor(255, 128, 128))
        , VariableName(variableName)
    {
        Category = NodeCategory::Variable;
        Type = NodeType::SimpleGet;
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
        Compiler& compiler = compilerCtx.compiler;

        Token varToken(TokenType::VAR, VariableName.c_str(), VariableName.length(), 0);
        compiler.emitVariable(varToken, false);

        GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[0]);
    }

    std::string VariableName;
};

static NodePtr BuildGetVariableNode(IDGenerator& IDGenerator, const char* variableName, PinType type)
{
    NodePtr node = std::make_shared<GetVariableNode>(IDGenerator.GetNextId(), "Get", variableName);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), variableName, type);

    return node;
}

struct SetVariableNode : public Node
{
    SetVariableNode(int id, const char* name, const char* variableName)
        : Node(id, name, ImColor(255, 128, 128))
        , VariableName(variableName)
    {
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
        Compiler& compiler = compilerCtx.compiler;

        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], InputValues[1]);

        Token varToken(TokenType::VAR, VariableName.c_str(), VariableName.length(), 0);
        compiler.emitVariable(varToken, true);
    }

    std::string VariableName;
};

static NodePtr BuildSetVariableNode(IDGenerator& IDGenerator, const char* variableName, PinType type)
{
    NodePtr node = std::make_shared<SetVariableNode>(IDGenerator.GetNextId(), "Set", variableName);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), variableName, type);

    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);

    node->InputValues.push_back(Value());
    node->InputValues.push_back(Value());

    return node;
}