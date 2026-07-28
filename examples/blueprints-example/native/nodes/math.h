
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"
#include "variadic.h"

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
        DefinitionFlags |= NodeDefinitionFlags::Pure;
        Type = NodeType::SimpleLargeBody;
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

        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[0], InputValues[0]);
        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], InputValues[1]);
        compiler.emitByte(OpByte(OP_CODE));

        GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[0]);
    }
};

template<OpCode OP_CODE>
struct VariadicOpNode : public VariadicInputNode
{
    VariadicOpNode(int id, const char* name)
        : VariadicInputNode(id, name, ImColor(230, 230, 0))
    {
        Category = NodeCategory::Function;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
        Type = NodeType::SimpleLargeBody;
        ShowInputPinNames = false;
        ShowOutputPinNames = false;
    }

    void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInputs:
            if (!GraphUtils::IsNodeImplicit(this))
                CompileInputs(compilerCtx, graph);
            break;
        case CompilationStage::PullOutput:
            if (GraphUtils::IsNodeImplicit(this))
                CompileInputs(compilerCtx, graph);
            break;
        default:
            break;
        }
    }

    void CompileInputs(CompilerContext& compilerCtx, const Graph& graph) const
    {
        Compiler& compiler = compilerCtx.compiler;
        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[0], InputValues[0]);

        for (size_t inputIndex = 1; inputIndex < Inputs.size(); ++inputIndex)
        {
            GraphCompiler::CompileInput(compilerCtx, graph, Inputs[inputIndex], InputValues[inputIndex]);
            compiler.emitByte(OpByte(OP_CODE));
        }

        GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[0]);
    }
};

template<class Node>
static NodePtr CreateBinaryNode(IDGenerator& IDGenerator, const char* name, const char* inputA,
                                const char* inputB, const char* output,
                                PinType outputType = PinType::Float,
                                PinType inputType = PinType::Float)
{
    NodePtr node = std::make_shared<Node>(IDGenerator.GetNextId(), name);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), inputA, inputType);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), inputB, inputType);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), output, outputType);

    node->InputValues.emplace_back(
        inputType == PinType::Any ? Value() : Value(0.0));
    node->InputValues.emplace_back(
        inputType == PinType::Any ? Value() : Value(0.0));
    return node;
}

using AddNode = VariadicOpNode<OpCode::OP_ADD>;
using SubtractNode = VariadicOpNode<OpCode::OP_SUBTRACT>;
using MultiplyNode = VariadicOpNode<OpCode::OP_MULTIPLY>;
using MinNode = VariadicOpNode<OpCode::OP_MIN>;
using MaxNode = VariadicOpNode<OpCode::OP_MAX>;
using DivideNode = BinaryOpNode<OpCode::OP_DIVIDE>;
using GreaterNode = BinaryOpNode<OpCode::OP_GREATER>;
using LessNode = BinaryOpNode<OpCode::OP_LESS>;
using EqualsNode = BinaryOpNode<OpCode::OP_EQUAL>;
using ModuloNode = BinaryOpNode<OpCode::OP_MODULO>;

static NodePtr CreateAddNode(IDGenerator& IDGenerator) { return CreateBinaryNode<AddNode>(IDGenerator, "+", "A", "B", "Result"); }
static NodePtr CreateSubtractNode(IDGenerator& IDGenerator) { return CreateBinaryNode<SubtractNode>(IDGenerator, "-", "A", "B", "Result"); }
static NodePtr CreateMultiplyNode(IDGenerator& IDGenerator) { return CreateBinaryNode<MultiplyNode>(IDGenerator, "x", "A", "B", "Result"); }
static NodePtr CreateMinNode(IDGenerator& IDGenerator) { return CreateBinaryNode<MinNode>(IDGenerator, "Min", "A", "B", "Result"); }
static NodePtr CreateMaxNode(IDGenerator& IDGenerator) { return CreateBinaryNode<MaxNode>(IDGenerator, "Max", "A", "B", "Result"); }
static NodePtr CreateDivideNode(IDGenerator& IDGenerator) { return CreateBinaryNode<DivideNode>(IDGenerator, "/", "", "", ""); }
static NodePtr CreateGreaterNode(IDGenerator& IDGenerator) { return CreateBinaryNode<GreaterNode>(IDGenerator, ">", "", "", "", PinType::Bool); }
static NodePtr CreateLessNode(IDGenerator& IDGenerator) { return CreateBinaryNode<LessNode>(IDGenerator, "<", "", "", "", PinType::Bool); }
static NodePtr CreateEqualsNode(IDGenerator& IDGenerator)
{
    NodePtr node = CreateBinaryNode<EqualsNode>(
        IDGenerator, "=", "", "", "", PinType::Bool, PinType::Any);
    const TypeRef comparable = TypeRef::Variable("T");
    node->Inputs[0].Type = node->Inputs[0].DeclaredType = comparable;
    node->Inputs[1].Type = node->Inputs[1].DeclaredType = comparable;
    return node;
}
static NodePtr CreateModuloNode(IDGenerator& IDGenerator) { return CreateBinaryNode<ModuloNode>(IDGenerator, "Mod", "Dividend", "Modulus", "Remainder"); }
