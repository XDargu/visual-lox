
# pragma once

#include "functionShared.h"

#include "../graphs/node.h"
#include "../graphs/graph.h"
#include "../graphs/idgeneration.h"
#include "../graphs/graphCompiler.h"

#include <Compiler.h>



struct FunctionNode : public Node
{
    FunctionNode(int id, const char* name, const BasicFunctionDefPtr& pFunctionDef)
        : Node(id, name, ImColor(255, 128, 128))
        , pFunctionDef(pFunctionDef)
    {
        Category = NodeCategory::Function;
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

        // Load named variable (native func)
        compiler.namedVariable(Token(TokenType::STRING, Name.c_str(), Name.length(), 10), false);

        int argCount = 0;
        // Gather Inputs normally
        for (int i = 0; i < Inputs.size(); ++i)
        {
            if (Inputs[i].Type != PinType::Flow)
            {
                GraphCompiler::CompileInput(compilerCtx, graph, Inputs[i], InputValues[i]);
                argCount++;
            }
        }

        if (HasFlag(Flags, NodeFlags::DynamicInputs))
        {
            // Get all inputs on a list first
            compiler.emitByte(OpByte(OpCode::OP_BUILD_LIST));
            compiler.emitByte(argCount);

            // We will only call the function with the list!
            argCount = 1;
        }

        compiler.emitBytes(OpByte(OpCode::OP_CALL), argCount);

        if (pFunctionDef->outputs.size() > 0)
        {
            // Set the output variable
            const int dataOutputIdx = GraphUtils::IsNodeImplicit(this) ? 0 : 1;
            GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[dataOutputIdx]);
        }
        else
        {
            compiler.emitByte(OpByte(OpCode::OP_POP));
        }
    }

    virtual void AddInput(IDGenerator& IDGenerator) override
    {
        Inputs.emplace_back(IDGenerator.GetNextId(), GetInputName(Inputs.size()).c_str(), pFunctionDef->dynamicInputProps.type);
        InputValues.emplace_back(pFunctionDef->dynamicInputProps.defaultValue);
    };

    virtual void RemoveInput(ed::PinId pinId) override
    {
        const int inputIdx = GraphUtils::FindNodeInputIdx(this, pinId);
        if (inputIdx != -1)
        {
            Inputs.erase(Inputs.begin() + inputIdx);
            InputValues.erase(InputValues.begin() + inputIdx);

            // Rename inputs!
            for (int i = 1; i < Inputs.size(); ++i)
            {
                Inputs[i].Name = GetInputName(i);
            }
        }
    };

    // TODO: Should be defined in the function def
    virtual bool CanRemoveInput(ed::PinId pinId) const override { return Inputs.size() > pFunctionDef->dynamicInputProps.minInputs; };
    virtual bool CanAddInput() const override { return Inputs.size() < pFunctionDef->dynamicInputProps.maxInputs; };

    static std::string GetInputName(int inputIdx) { return std::string(1, char(65 + inputIdx)); }

    BasicFunctionDefPtr pFunctionDef;
};

NodePtr BasicFunctionDef::MakeNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<FunctionNode>(IDGenerator.GetNextId(), name.c_str(), shared_from_this());

    if (!HasFlag(flags, NodeFlags::ReadOnly))
    {
        node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
        node->InputValues.emplace_back(Value());

        node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    }

    if (!HasFlag(flags, NodeFlags::DynamicInputs))
    {
        for (const Input& input : inputs)
        {
            node->Inputs.emplace_back(IDGenerator.GetNextId(), input.name.c_str(), TypeOfValue(input.value));
            node->InputValues.emplace_back(input.value);
        }
    }

    for (const Input& output : outputs)
    {
        node->Outputs.emplace_back(IDGenerator.GetNextId(), output.name.c_str(), TypeOfValue(output.value));
    }

    node->Flags = flags;

    return node;
}