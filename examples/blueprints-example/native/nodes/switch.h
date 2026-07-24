#pragma once

#include "../../graphs/graphCompiler.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include <Compiler.h>

struct SwitchFlowNode : public Node
{
    explicit SwitchFlowNode(int id)
        : Node(id, "Switch", ImColor(255, 255, 255))
    {
        Category = NodeCategory::Flow;
    }

    bool IsInputDeferred(int inputIndex) const override
    {
        return inputIndex >= 1;
    }

    bool ShouldCompileDeferredInput(int inputIndex, int outputIndex) const override
    {
        return outputIndex >= 0 &&
               outputIndex < static_cast<int>(Outputs.size()) - 1 &&
               inputIndex == outputIndex + 1;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int portIdx) const override
    {
        Compiler& compiler = context.compiler;
        const int caseCount = static_cast<int>(Inputs.size()) - 1;

        switch (stage)
        {
        case CompilationStage::BeginInputs:
            failureJumps.assign(caseCount, 0);
            successJumps.clear();
            break;
        case CompilationStage::BeforeOutput:
            if (portIdx < caseCount)
            {
                conditionLocalStart = compiler.current->localCount;
                compiler.beginScope();
            }
            break;
        case CompilationStage::BeginOutput:
            if (portIdx < caseCount)
            {
                conditionLocalCount =
                    compiler.current->localCount - conditionLocalStart;
                GraphCompiler::CompileInput(
                    context, graph, Inputs[portIdx + 1], InputValues[portIdx + 1]);
                failureJumps[portIdx] =
                    compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
                compiler.emitByte(OpByte(OpCode::OP_POP));
            }
            compiler.beginScope();
            break;
        case CompilationStage::EndOutput:
            compiler.endScope();
            if (portIdx < caseCount)
            {
                // Remove locals created while evaluating this condition on the
                // success path, then mirror that cleanup on the false path.
                compiler.endScope();
                successJumps.push_back(
                    compiler.emitJump(OpByte(OpCode::OP_JUMP)));
                compiler.patchJump(failureJumps[portIdx]);
                compiler.emitByte(OpByte(OpCode::OP_POP));
                for (int i = 0; i < conditionLocalCount; ++i)
                    compiler.emitByte(OpByte(OpCode::OP_POP));
            }
            break;
        case CompilationStage::EndInputs:
            for (size_t jump : successJumps)
                compiler.patchJump(jump);
            break;
        default:
            break;
        }
    }

    void AddInput(IDGenerator& ids) override
    {
        const int caseNumber = static_cast<int>(Inputs.size());
        Inputs.emplace_back(
            ids.GetNextId(), ("Condition " + std::to_string(caseNumber)).c_str(),
            PinType::Bool);
        InputValues.emplace_back(Value(false));
        Outputs.insert(
            Outputs.end() - 1,
            Pin(ids.GetNextId(), ("Case " + std::to_string(caseNumber)).c_str(),
                PinType::Flow));
    }

    void RemoveInput(ed::PinId pinId) override
    {
        const int inputIndex = GraphUtils::FindNodeInputIdx(this, pinId);
        if (inputIndex < 1)
            return;

        Inputs.erase(Inputs.begin() + inputIndex);
        InputValues.erase(InputValues.begin() + inputIndex);
        Outputs.erase(Outputs.begin() + inputIndex - 1);
        for (int i = 1; i < static_cast<int>(Inputs.size()); ++i)
        {
            Inputs[i].Name = "Condition " + std::to_string(i);
            Outputs[i - 1].Name = "Case " + std::to_string(i);
        }
    }

    bool CanRemoveInput(ed::PinId pinId) const override
    {
        return Inputs.size() > 2 &&
               GraphUtils::FindNodeInputIdx(this, pinId) >= 1;
    }

    bool CanAddInput() const override { return Inputs.size() < 17; }

    mutable std::vector<size_t> failureJumps;
    mutable std::vector<size_t> successJumps;
    mutable int conditionLocalStart = 0;
    mutable int conditionLocalCount = 0;
};

inline NodePtr BuildSwitchFlowNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<SwitchFlowNode>(ids.GetNextId());
    node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(
        ids.GetNextId(), "Condition 1", PinType::Bool);
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(false));
    node->Outputs.emplace_back(
        ids.GetNextId(), "Case 1", PinType::Flow);
    node->Outputs.emplace_back(
        ids.GetNextId(), "Default", PinType::Flow);
    return node;
}
