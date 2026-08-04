#pragma once

#include "../../graphs/graphCompiler.h"
#include "../../graphs/idgeneration.h"
#include "variadic.h"

#include <Compiler.h>

template<OpCode Operation>
struct UnaryExpressionNode : public Node
{
    UnaryExpressionNode(int id, const char* name)
        : Node(id, name, ImColor(230, 230, 0))
    {
        Category = NodeCategory::Function;
        Type = NodeType::SimpleLargeBody;
        ShowInputPinNames = false;
        ShowOutputPinNames = false;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::PullOutput)
            return;
        GraphCompiler::CompileInput(context, graph, Inputs[0], Inputs[0].LiteralValue);
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
        ShowInputPinNames = false;
        ShowOutputPinNames = false;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::PullOutput)
            return;
        GraphCompiler::CompileInput(context, graph, Inputs[0], Inputs[0].LiteralValue);
        GraphCompiler::CompileInput(context, graph, Inputs[1], Inputs[1].LiteralValue);
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

struct ShortCircuitExpressionNode : public VariadicInputNode
{
    ShortCircuitExpressionNode(int id, const char* name, ShortCircuitMode mode)
        : VariadicInputNode(id, name, ImColor(230, 230, 0))
        , mode(mode)
    {
        Category = NodeCategory::Function;
        Type = NodeType::SimpleLargeBody;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    bool IsInputDeferred(int inputIndex) const override
    {
        return inputIndex > 0;
    }

    bool ShouldCompileDeferredInput(int inputIndex, int outputIndex) const override
    {
        return inputIndex > 0 && outputIndex == -1;
    }

    bool CanRemoveInput(ed::PinId pinId) const override
    {
        return mode != ShortCircuitMode::Coalesce && VariadicInputNode::CanRemoveInput(pinId);
    }

    bool CanAddInput() const override
    {
        return mode != ShortCircuitMode::Coalesce && VariadicInputNode::CanAddInput();
    }

    Token OutputToken(CompilerContext& context) const
    {
        const std::string name =
            std::string(CompilerContext::tempVarPrefix) +
            std::to_string(Outputs[0].ID.Get());
        return context.StoreTempVariable(name);
    }

    void Compile(CompilerContext& context, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        Compiler& compiler = context.compiler;
        const Token outputToken = OutputToken(context);

        if (stage == CompilationStage::BeforeDeferredInput)
        {
            if (portIdx == 1)
            {
                exitJumps.clear();
                successJumps.clear();
                GraphCompiler::CompileInput(context, graph, Inputs[0], Inputs[0].LiteralValue);
                compiler.addLocal(outputToken, true);
                compiler.emitVariable(outputToken, true, true);
            }

            compiler.emitVariable(outputToken, false);
            if (mode == ShortCircuitMode::Coalesce)
                compiler.emitByte(OpByte(OpCode::OP_IS_NIL));

            const size_t branchJump = compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
            compiler.emitByte(OpByte(OpCode::OP_POP));

            if (mode == ShortCircuitMode::Or)
            {
                successJumps.push_back(compiler.emitJump(OpByte(OpCode::OP_JUMP)));
                compiler.patchJump(branchJump);
                compiler.emitByte(OpByte(OpCode::OP_POP));
            }
            else
            {
                exitJumps.push_back(branchJump);
            }

            compiler.beginScope();
        }
        else if (stage == CompilationStage::AfterDeferredInput)
        {
            GraphCompiler::CompileInput(context, graph, Inputs[portIdx], Inputs[portIdx].LiteralValue);
            compiler.emitVariable(outputToken, true, true);
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.endScope();
        }
        else if (stage == CompilationStage::PullOutput)
        {
            if (mode == ShortCircuitMode::Or)
            {
                for (size_t jump : successJumps)
                    compiler.patchJump(jump);
                return;
            }

            const size_t endJump = compiler.emitJump(OpByte(OpCode::OP_JUMP));
            for (size_t jump : exitJumps)
                compiler.patchJump(jump);
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.patchJump(endJump);
        }
    }

    ShortCircuitMode mode;
    mutable std::vector<size_t> exitJumps;
    mutable std::vector<size_t> successJumps;
};

inline NodePtr BuildUnaryExpressionNode(IDGenerator& ids, const char* name,
                                        PinType inputType, PinType outputType,
                                        OpCode operation, const Value& defaultValue)
{
    NodePtr node;

    if (operation == OpCode::OP_NEGATE)
        node = std::make_shared<UnaryExpressionNode<OpCode::OP_NEGATE>>(ids.GetNextId(), name);
    else if (operation == OpCode::OP_IS_NIL)
        node = std::make_shared<UnaryExpressionNode<OpCode::OP_IS_NIL>>(ids.GetNextId(), name);
    else if (operation == OpCode::OP_TO_STRING)
        node = std::make_shared<UnaryExpressionNode<OpCode::OP_TO_STRING>>(ids.GetNextId(), name);
    else
        node = std::make_shared<UnaryExpressionNode<OpCode::OP_NOT>>(ids.GetNextId(), name);

    node->Inputs.emplace_back(ids.GetNextId(), "Value", inputType);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", outputType);
    node->Inputs.back().LiteralValue = defaultValue;
    return node;
}

inline NodePtr BuildNotNode(IDGenerator& ids)
{
    return BuildUnaryExpressionNode(ids, "Not", PinType::Bool, PinType::Bool, OpCode::OP_NOT, Value(false));
}

inline NodePtr BuildNegateNode(IDGenerator& ids)
{
    return BuildUnaryExpressionNode(ids, "Negate", PinType::Float, PinType::Float, OpCode::OP_NEGATE, Value(0.0));
}

inline NodePtr BuildIsNilNode(IDGenerator& ids)
{
    return BuildUnaryExpressionNode(ids, "Is Nil", PinType::Any, PinType::Bool, OpCode::OP_IS_NIL, Value());
}

inline NodePtr BuildToStringNode(IDGenerator& ids)
{
    return BuildUnaryExpressionNode(ids, "ToString", PinType::Any, PinType::String, OpCode::OP_TO_STRING, Value());
}

inline NodePtr BuildNotEqualsNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<TwoOperationExpressionNode<OpCode::OP_EQUAL, OpCode::OP_NOT>>(ids.GetNextId(), "!=");

    const TypeRef comparable = TypeRef::Variable("T");
    node->Inputs.emplace_back(ids.GetNextId(), "A", comparable);
    node->Inputs.emplace_back(ids.GetNextId(), "B", comparable);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", PinType::Bool);

    return node;
}

inline NodePtr BuildGreaterOrEqualNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<TwoOperationExpressionNode<OpCode::OP_LESS, OpCode::OP_NOT>>(ids.GetNextId(), ">=");

    node->Inputs.emplace_back(ids.GetNextId(), "A", PinType::Float);
    node->Inputs.emplace_back(ids.GetNextId(), "B", PinType::Float);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", PinType::Bool);
    node->Inputs[0].LiteralValue = Value(0.0);
    node->Inputs[1].LiteralValue = Value(0.0);

    return node;
}

inline NodePtr BuildLessOrEqualNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<TwoOperationExpressionNode<OpCode::OP_GREATER, OpCode::OP_NOT>>(ids.GetNextId(), "<=");

    node->Inputs.emplace_back(ids.GetNextId(), "A", PinType::Float);
    node->Inputs.emplace_back(ids.GetNextId(), "B", PinType::Float);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", PinType::Bool);
    node->Inputs[0].LiteralValue = Value(0.0);
    node->Inputs[1].LiteralValue = Value(0.0);

    return node;
}

inline NodePtr BuildShortCircuitNode(IDGenerator& ids, const char* name,
                                     ShortCircuitMode mode, PinType type,
                                     const Value& defaultValue)
{
    NodePtr node = std::make_shared<ShortCircuitExpressionNode>(ids.GetNextId(), name, mode);

    node->Inputs.emplace_back(ids.GetNextId(), "A", type);
    node->Inputs.emplace_back(ids.GetNextId(), "B", type);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", type);
    node->Inputs[0].LiteralValue = defaultValue;
    node->Inputs[1].LiteralValue = defaultValue;

    return node;
}

inline NodePtr BuildAndNode(IDGenerator& ids)
{
    NodePtr node = BuildShortCircuitNode(ids, "And", ShortCircuitMode::And, PinType::Bool, Value(false));

    node->ShowInputPinNames = false;
    node->ShowOutputPinNames = false;

    return node;
}

inline NodePtr BuildOrNode(IDGenerator& ids)
{
    NodePtr node = BuildShortCircuitNode(ids, "Or", ShortCircuitMode::Or, PinType::Bool, Value(false));

    node->ShowInputPinNames = false;
    node->ShowOutputPinNames = false;

    return node;
}

inline NodePtr BuildCoalesceNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<ShortCircuitExpressionNode>(ids.GetNextId(), "Coalesce", ShortCircuitMode::Coalesce);

    const TypeRef valueType = TypeRef::Variable("T");
    node->Inputs.emplace_back(ids.GetNextId(), "Value", valueType);
    node->Inputs.emplace_back(ids.GetNextId(), "Fallback", valueType);
    node->Outputs.emplace_back(ids.GetNextId(), "Result", valueType);

    return node;
}
