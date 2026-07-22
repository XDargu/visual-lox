#pragma once

#include "../../graphs/graphCompiler.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include <Compiler.h>

struct MatchFlowNode : public Node
{
    explicit MatchFlowNode(int id)
        : Node(id, "Match", ImColor(255, 255, 255))
    {
        Category = NodeCategory::Flow;
    }

    std::string MatchVariableName() const
    {
        return "__match_" + std::to_string(ID.Get());
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int portIdx) const override
    {
        Compiler& compiler = context.compiler;
        const int caseCount = static_cast<int>(Inputs.size()) - 2;
        const Token valueToken = context.StoreTempVariable(MatchVariableName());

        switch (stage)
        {
        case CompilationStage::BeginInputs:
            compiler.beginScope();
            compiler.addLocal(valueToken, false);
            GraphCompiler::CompileInput(context, graph, Inputs[1], InputValues[1]);
            compiler.emitVariable(valueToken, true);
            failureJumps.assign(caseCount, 0);
            successJumps.clear();
            break;
        case CompilationStage::BeginOutput:
            if (portIdx < caseCount)
            {
                compiler.namedVariable(valueToken, false);
                GraphCompiler::CompileInput(context, graph, Inputs[portIdx + 2],
                                            InputValues[portIdx + 2]);
                compiler.emitByte(OpByte(OpCode::OP_MATCH));
                failureJumps[portIdx] = compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
                compiler.emitByte(OpByte(OpCode::OP_POP));
            }
            compiler.beginScope();
            break;
        case CompilationStage::EndOutput:
            compiler.endScope();
            if (portIdx < caseCount)
            {
                successJumps.push_back(compiler.emitJump(OpByte(OpCode::OP_JUMP)));
                compiler.patchJump(failureJumps[portIdx]);
                compiler.emitByte(OpByte(OpCode::OP_POP));
            }
            break;
        case CompilationStage::EndInputs:
            for (size_t jump : successJumps)
                compiler.patchJump(jump);
            compiler.endScope();
            break;
        default:
            break;
        }
    }

    void AddInput(IDGenerator& ids) override
    {
        const int caseNumber = static_cast<int>(Inputs.size()) - 1;
        Inputs.emplace_back(ids.GetNextId(), ("Pattern " + std::to_string(caseNumber)).c_str(), PinType::Any);
        InputValues.emplace_back(Value(0.0));
        Outputs.insert(Outputs.end() - 1,
            Pin(ids.GetNextId(), ("Case " + std::to_string(caseNumber)).c_str(), PinType::Flow));
    }

    void RemoveInput(ed::PinId pinId) override
    {
        const int inputIndex = GraphUtils::FindNodeInputIdx(this, pinId);
        if (inputIndex < 2)
            return;
        const int caseIndex = inputIndex - 2;
        Inputs.erase(Inputs.begin() + inputIndex);
        InputValues.erase(InputValues.begin() + inputIndex);
        Outputs.erase(Outputs.begin() + caseIndex);
        for (int i = 2; i < static_cast<int>(Inputs.size()); ++i)
        {
            const int number = i - 1;
            Inputs[i].Name = "Pattern " + std::to_string(number);
            Outputs[i - 2].Name = "Case " + std::to_string(number);
        }
    }

    bool CanRemoveInput(ed::PinId pinId) const override
    {
        return Inputs.size() > 3 && GraphUtils::FindNodeInputIdx(this, pinId) >= 2;
    }

    bool CanAddInput() const override { return Inputs.size() < 18; }

    mutable std::vector<size_t> failureJumps;
    mutable std::vector<size_t> successJumps;
};

inline NodePtr BuildMatchFlowNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<MatchFlowNode>(ids.GetNextId());
    node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(ids.GetNextId(), "Value", PinType::Any);
    node->Inputs.emplace_back(ids.GetNextId(), "Pattern 1", PinType::Any);
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(0.0));
    node->Outputs.emplace_back(ids.GetNextId(), "Case 1", PinType::Flow);
    node->Outputs.emplace_back(ids.GetNextId(), "Default", PinType::Flow);
    return node;
}
