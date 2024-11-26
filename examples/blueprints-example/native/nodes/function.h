
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct GetFunctionNode : public Node
{
    GetFunctionNode(int id, const char* name, const BasicFunctionDefPtr& pFunctionDef)
        : Node(id, name, ImColor(255, 128, 128))
        , pFunctionDef(pFunctionDef)
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

        Token varToken(TokenType::VAR, pFunctionDef->name.c_str(), pFunctionDef->name.length(), 0);
        compiler.emitVariable(varToken, false);

        GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[0]);
    }

    void Refresh(IDGenerator& IDGenerator) override
    {
        Outputs[0].Name = pFunctionDef->name;
    }

    BasicFunctionDefPtr pFunctionDef;
};

static NodePtr BuildGetFunctionNode(IDGenerator& IDGenerator, const BasicFunctionDefPtr& pFunctionDef)
{
    NodePtr node = std::make_shared<GetFunctionNode>(IDGenerator.GetNextId(), "", pFunctionDef);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), pFunctionDef->name.c_str(), PinType::Function);

    return node;
}