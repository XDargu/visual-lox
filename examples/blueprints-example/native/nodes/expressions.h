#pragma once

#include "../../graphs/graphCompiler.h"
#include "../../graphs/idgeneration.h"

#include <Compiler.h>

template<OpCode Operation>
struct UnaryExpressionNode : public Node
{
    UnaryExpressionNode(int id, const char* name)
        : Node(id, name, ImColor(230, 230, 0))
    {
        Category = NodeCategory::Function;
        Type = NodeType::SimpleLargeBody;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::PullOutput)
            return;
        GraphCompiler::CompileInput(context, graph, Inputs[0], InputValues[0]);
        context.compiler.emitByte(OpByte(Operation));
        GraphCompiler::CompileOutput(context, graph, Outputs[0]);
    }
};

template<OpCode FirstOperation, OpCode SecondOperation>
struct TwoOperationExpressionNode : public Node
{
    TwoOperationExpressionNode(int id, const char* name)
        : Node(id, name, ImColor(230, 230, 0))
    {
        Category = NodeCategory::Function;
        Type = NodeType::SimpleLargeBody;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::PullOutput)
            return;
        GraphCompiler::CompileInput(context, graph, Inputs[0], InputValues[0]);
        GraphCompiler::CompileInput(context, graph, Inputs[1], InputValues[1]);
        context.compiler.emitByte(OpByte(FirstOperation));
        context.compiler.emitByte(OpByte(SecondOperation));
        GraphCompiler::CompileOutput(context, graph, Outputs[0]);
    }
};

enum class ShortCircuitMode
{
    And,
    Or,
    Coalesce,
};

struct ShortCircuitExpressionNode : public Node
{
    ShortCircuitExpressionNode(int id, const char* name, ShortCircuitMode mode)
        : Node(id, name, ImColor(230, 230, 0))
        , mode(mode)
    {
        Category = NodeCategory::Function;
        Type = NodeType::SimpleLargeBody;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    bool IsInputDeferred(int inputIndex) const override
    {
        return inputIndex == 1;
    }

    bool ShouldCompileDeferredInput(int inputIndex, int outputIndex) const override
    {
        return inputIndex == 1 && outputIndex == -1;
    }

    Token OutputToken(CompilerContext& context) const
    {
        const std::string name =
            std::string(CompilerContext::tempVarPrefix) +
            std::to_string(Outputs[0].ID.Get());
        return context.StoreTempVariable(name);
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        Compiler& compiler = context.compiler;
        const Token outputToken = OutputToken(context);

        if (stage == CompilationStage::BeforeDeferredInput)
        {
            GraphCompiler::CompileInput(context, graph, Inputs[0], InputValues[0]);
            compiler.addLocal(outputToken, true);
            compiler.emitVariable(outputToken, true, true);
            compiler.emitVariable(outputToken, false);
            if (mode == ShortCircuitMode::Coalesce)
                compiler.emitByte(OpByte(OpCode::OP_IS_NIL));

            branchJump = compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
            compiler.emitByte(OpByte(OpCode::OP_POP));

            if (mode == ShortCircuitMode::Or)
            {
                endJump = compiler.emitJump(OpByte(OpCode::OP_JUMP));
                compiler.patchJump(branchJump);
                compiler.emitByte(OpByte(OpCode::OP_POP));
            }
            compiler.beginScope();
        }
        else if (stage == CompilationStage::PullOutput)
        {
            GraphCompiler::CompileInput(context, graph, Inputs[1], InputValues[1]);
            compiler.emitVariable(outputToken, true, true);
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.endScope();

            if (mode == ShortCircuitMode::Or)
            {
                compiler.patchJump(endJump);
            }
            else
            {
                endJump = compiler.emitJump(OpByte(OpCode::OP_JUMP));
                compiler.patchJump(branchJump);
                compiler.emitByte(OpByte(OpCode::OP_POP));
                compiler.patchJump(endJump);
            }
        }
    }

    ShortCircuitMode mode;
    mutable size_t branchJump = 0;
    mutable size_t endJump = 0;
};

inline NodePtr BuildUnaryExpressionNode(IDGenerator& ids, const char* name,
                                        PinType inputType, PinType outputType,
                                        OpCode operation, const Value& defaultValue)
{
    NodePtr node;
    if (operation == OpCode::OP_NEGATE)
        node = std::make_shared<UnaryExpressionNode<OpCode::OP_NEGATE>>(
            ids.GetNextId(), name);
    else if (operation == OpCode::OP_IS_NIL)
        node = std::make_shared<UnaryExpressionNode<OpCode::OP_IS_NIL>>(
            ids.GetNextId(), name);
    else
        node = std::make_shared<UnaryExpressionNode<OpCode::OP_NOT>>(
            ids.GetNextId(), name);
    node->Inputs.emplace_back(ids.GetNextId(), "Value", inputType);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", outputType);
    node->InputValues.emplace_back(defaultValue);
    return node;
}

inline NodePtr BuildNotNode(IDGenerator& ids)
{
    return BuildUnaryExpressionNode(
        ids, "Not", PinType::Bool, PinType::Bool, OpCode::OP_NOT, Value(false));
}

inline NodePtr BuildNegateNode(IDGenerator& ids)
{
    return BuildUnaryExpressionNode(
        ids, "Negate", PinType::Float, PinType::Float, OpCode::OP_NEGATE, Value(0.0));
}

inline NodePtr BuildIsNilNode(IDGenerator& ids)
{
    return BuildUnaryExpressionNode(
        ids, "Is Nil", PinType::Any, PinType::Bool, OpCode::OP_IS_NIL, Value());
}

inline NodePtr BuildNotEqualsNode(IDGenerator& ids)
{
    NodePtr node =
        std::make_shared<TwoOperationExpressionNode<OpCode::OP_EQUAL, OpCode::OP_NOT>>(
            ids.GetNextId(), "!=");
    node->Inputs.emplace_back(ids.GetNextId(), "A", PinType::Any);
    node->Inputs.emplace_back(ids.GetNextId(), "B", PinType::Any);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", PinType::Bool);
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value());
    return node;
}

inline NodePtr BuildGreaterOrEqualNode(IDGenerator& ids)
{
    NodePtr node =
        std::make_shared<TwoOperationExpressionNode<OpCode::OP_LESS, OpCode::OP_NOT>>(
            ids.GetNextId(), ">=");
    node->Inputs.emplace_back(ids.GetNextId(), "A", PinType::Float);
    node->Inputs.emplace_back(ids.GetNextId(), "B", PinType::Float);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", PinType::Bool);
    node->InputValues.emplace_back(Value(0.0));
    node->InputValues.emplace_back(Value(0.0));
    return node;
}

inline NodePtr BuildLessOrEqualNode(IDGenerator& ids)
{
    NodePtr node =
        std::make_shared<TwoOperationExpressionNode<OpCode::OP_GREATER, OpCode::OP_NOT>>(
            ids.GetNextId(), "<=");
    node->Inputs.emplace_back(ids.GetNextId(), "A", PinType::Float);
    node->Inputs.emplace_back(ids.GetNextId(), "B", PinType::Float);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", PinType::Bool);
    node->InputValues.emplace_back(Value(0.0));
    node->InputValues.emplace_back(Value(0.0));
    return node;
}

inline NodePtr BuildShortCircuitNode(IDGenerator& ids, const char* name,
                                     ShortCircuitMode mode, PinType type,
                                     const Value& defaultValue)
{
    NodePtr node = std::make_shared<ShortCircuitExpressionNode>(
        ids.GetNextId(), name, mode);
    node->Inputs.emplace_back(ids.GetNextId(), "A", type);
    node->Inputs.emplace_back(ids.GetNextId(), "B", type);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", type);
    node->InputValues.emplace_back(defaultValue);
    node->InputValues.emplace_back(defaultValue);
    return node;
}

inline NodePtr BuildAndNode(IDGenerator& ids)
{
    return BuildShortCircuitNode(
        ids, "And", ShortCircuitMode::And, PinType::Bool, Value(false));
}

inline NodePtr BuildOrNode(IDGenerator& ids)
{
    return BuildShortCircuitNode(
        ids, "Or", ShortCircuitMode::Or, PinType::Bool, Value(false));
}

inline NodePtr BuildCoalesceNode(IDGenerator& ids)
{
    return BuildShortCircuitNode(
        ids, "Coalesce", ShortCircuitMode::Coalesce, PinType::Any, Value());
}
