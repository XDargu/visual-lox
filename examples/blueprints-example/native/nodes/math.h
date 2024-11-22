
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

template<OpCode OP_CODE>
struct BinaryOpNode : public Node
{
    BinaryOpNode(int id, const char* name)
        : Node(id, name, ImColor(230, 230, 0))
    {
        Category = NodeCategory::Function;
        Flags |= NodeFlags::CanConstFold;
    }

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInputs:
        {
            if (!GraphUtils::IsNodeImplicit(this))
                CompileInputs(compiler, graph);
        }
        break;
        case CompilationStage::PullOutput:
        {
            if (GraphUtils::IsNodeImplicit(this))
                CompileInputs(compiler, graph);
        }
        break;
        }
    }

    void CompileInputs(Compiler& compiler, const Graph& graph) const
    {
        GraphCompiler::CompileInput(compiler, graph, Inputs[0], InputValues[0]);
        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
        compiler.emitByte(OpByte(OP_CODE));

        GraphCompiler::CompileOutput(compiler, graph, Outputs[0]);
    }
};

template<class Node>
static NodePtr CreateBinaryNode(IDGenerator& IDGenerator, const char* name, const char* inputA, const char* inputB, const char* output, PinType outputType = PinType::Float)
{
    NodePtr node = std::make_shared<Node>(IDGenerator.GetNextId(), name);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), inputA, PinType::Float);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), inputB, PinType::Float);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), output, outputType);

    node->InputValues.emplace_back(Value(0.0f));
    node->InputValues.emplace_back(Value(0.0f));
    return node;
}

using AddNode = BinaryOpNode<OpCode::OP_ADD>;
using SubtractNode = BinaryOpNode<OpCode::OP_SUBTRACT>;
using MultiplyNode = BinaryOpNode<OpCode::OP_MULTIPLY>;
using DivideNode = BinaryOpNode<OpCode::OP_DIVIDE>;
using GreaterNode = BinaryOpNode<OpCode::OP_GREATER>;
using LessNode = BinaryOpNode<OpCode::OP_LESS>;
using ModuloNode = BinaryOpNode<OpCode::OP_MODULO>;

static NodePtr CreateAddNode(IDGenerator& IDGenerator) { return CreateBinaryNode<AddNode>(IDGenerator, "Add", "A", "B", "Result"); }
static NodePtr CreateSubtractNode(IDGenerator& IDGenerator) { return CreateBinaryNode<SubtractNode>(IDGenerator, "Subtract", "A", "B", "Result"); }
static NodePtr CreateMultiplyNode(IDGenerator& IDGenerator) { return CreateBinaryNode<MultiplyNode>(IDGenerator, "Multiply", "A", "B", "Result"); }
static NodePtr CreateDivideNode(IDGenerator& IDGenerator) { return CreateBinaryNode<DivideNode>(IDGenerator, "Divide", "A", "B", "Result"); }
static NodePtr CreateGreaterNode(IDGenerator& IDGenerator) { return CreateBinaryNode<GreaterNode>(IDGenerator, "Greater Than", "A", "B", "Result", PinType::Bool); }
static NodePtr CreateLessNode(IDGenerator& IDGenerator) { return CreateBinaryNode<LessNode>(IDGenerator, "Less Than", "A", "B", "Result", PinType::Bool); }
static NodePtr CreateModuloNode(IDGenerator& IDGenerator) { return CreateBinaryNode<ModuloNode>(IDGenerator, "Modulo", "Dividend", "Modulus", "Remainder"); }
