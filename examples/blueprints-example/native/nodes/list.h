
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct ListGetByIndex : public Node
{
    ListGetByIndex(int id, const char* name)
        : Node(id, name, ImColor(255, 128, 128))
    {
        Category = NodeCategory::Function;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        if (stage == CompilationStage::PullOutput)
            CompileInputs(compilerCtx, graph);
    }

    void CompileInputs(CompilerContext& compilerCtx, const Graph& graph) const
    {
        Compiler& compiler = compilerCtx.compiler;

        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[0], Inputs[0].LiteralValue);
        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], Inputs[1].LiteralValue);
        compiler.emitByte(OpByte(OpCode::OP_INDEX_SUBSCR));

        GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[0]);
    }
};

static NodePtr BuildListGetByIndexNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<ListGetByIndex>(IDGenerator.GetNextId(), "Get By Index");
    const TypeRef element = TypeRef::Variable("T");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "List", TypeRef::List(element));
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Index", PinType::Float);

    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Value", element);

    node->Inputs[0].LiteralValue = Value(newList());
    node->Inputs[1].LiteralValue = Value(0.0);
    return node;
}


struct ListSetByIndex : public Node
{
    ListSetByIndex(int id, const char* name)
        : Node(id, name, ImColor(255, 128, 128))
    {
        Category = NodeCategory::Function;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInputs:
        {
            CompileInputs(compilerCtx, graph);
        }
        break;
        }
    }

    void CompileInputs(CompilerContext& compilerCtx, const Graph& graph) const
    {
        Compiler& compiler = compilerCtx.compiler;

        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], Inputs[1].LiteralValue);
        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[2], Inputs[2].LiteralValue);
        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[3], Inputs[3].LiteralValue);
        compiler.emitByte(OpByte(OpCode::OP_STORE_SUBSCR));

        GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[1]);
    }
};

static NodePtr BuildListSetByIndexNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<ListSetByIndex>(IDGenerator.GetNextId(), "Set By Index");
    const TypeRef element = TypeRef::Variable("T");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "List", TypeRef::List(element));
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Index", PinType::Float);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Value", element);

    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Value", TypeRef::List(element));
    node->Inputs[1].LiteralValue = Value(newList());
    node->Inputs[2].LiteralValue = Value(0.0);
    return node;
}
