#pragma once

#include "../../graphs/graphCompiler.h"
#include "../../graphs/idgeneration.h"

#include <Compiler.h>

struct RangeNode : public Node
{
    RangeNode(int id, const char* name)
        : Node(id, name, ImColor(230, 153, 45))
    {
        Category = NodeCategory::Function;
        DefinitionFlags |= NodeDefinitionFlags::Pure;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int) const override
    {
        if (stage != CompilationStage::PullOutput)
            return;
        GraphCompiler::CompileInput(context, graph, Inputs[0], InputValues[0]);
        GraphCompiler::CompileInput(context, graph, Inputs[1], InputValues[1]);
        context.compiler.emitByte(OpByte(OpCode::OP_BUILD_RANGE));
        GraphCompiler::CompileOutput(context, graph, Outputs[0]);
    }
};

inline NodePtr BuildRangeNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<RangeNode>(ids.GetNextId(), "Range");
    node->Inputs.emplace_back(ids.GetNextId(), "From", PinType::Float);
    node->Inputs.emplace_back(ids.GetNextId(), "To", PinType::Float);
    node->Outputs.emplace_back(ids.GetNextId(), "Range", PinType::Range);
    node->InputValues.emplace_back(Value(0.0));
    node->InputValues.emplace_back(Value(1.0));
    return node;
}
