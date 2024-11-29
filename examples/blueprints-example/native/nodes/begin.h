
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"
#include "../../script/script.h"

#include "../../utilities/utils.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct BeginNode : public Node
{
    BeginNode(int id, const char* name, const BasicFunctionDefPtr& functionDef)
        : Node(id, name, ImColor(255, 255, 255))
        , functionDef(functionDef)
    {
        Category = NodeCategory::Begin;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
    }

    virtual void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        // Add missing outputs
        const int startingOutput = 1;

        for (int i = 0; i < functionDef->inputs.size(); ++i)
        {
            const BasicFunctionDef::Input& input = functionDef->inputs[i];

            if (Pin* existingOutput = FindOutputByName(input.name))
            {
                existingOutput->Type = TypeOfValue(input.value);
            }
            else
            {
                Outputs.insert(Outputs.begin() + startingOutput + i, { IDGenerator.GetNextId(), input.name.c_str(), TypeOfValue(input.value) });
            }
        }

        // Remove outputs that are no longer there
        stl::erase_if(Outputs, [&](const Pin& output)
        {
            return output.Type != PinType::Flow && functionDef->FindInputByName(output.Name) == nullptr;
        });
    }

    BasicFunctionDefPtr functionDef;
};

static NodePtr BuildBeginNode(IDGenerator& IDGenerator, const ScriptFunctionPtr& function)
{
    NodePtr node = std::make_shared<BeginNode>(IDGenerator.GetNextId(), "Begin", function->functionDef);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);

    for (int i = 0; i < function->functionDef->inputs.size(); ++i)
    {
        const BasicFunctionDef::Input& input = function->functionDef->inputs[i];
        node->Outputs.emplace_back(IDGenerator.GetNextId(),input.name.c_str(), TypeOfValue(input.value));
    }

    return node;
}

struct AppendNode : public Node
{
    AppendNode(int id, const char* name)
        : Node(id, name, ImColor(255, 128, 128))
    {
        Category = NodeCategory::Function;
        Flags |= NodeFlags::DynamicInputs;
        Flags |= NodeFlags::CanConstFold;
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

        for (int i = 1; i < Inputs.size(); ++i)
        {
            GraphCompiler::CompileInput(compilerCtx, graph, Inputs[i], InputValues[i]);
            compiler.emitByte(OpByte(OpCode::OP_ADD));
        }

        GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[0]);
    }

    virtual void AddInput(IDGenerator& IDGenerator) override
    {
        const char asciiChar = char(65 + Inputs.size());
        Inputs.emplace_back(IDGenerator.GetNextId(), std::string(1, asciiChar).c_str(), PinType::Any);
        InputValues.emplace_back(Value(copyString("", 0)));
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

    virtual bool CanRemoveInput(ed::PinId pinId) const override { return Inputs.size() > 2; };
    virtual bool CanAddInput() const override { return Inputs.size() < 16; };

    static std::string GetInputName(int inputIdx) { return std::string(1, char(65 + inputIdx)); }
};

static NodePtr CreateAppendNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<AppendNode>(IDGenerator.GetNextId(), "Append");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "A", PinType::Any);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "B", PinType::Any);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Result", PinType::String);

    node->InputValues.emplace_back(Value(copyString("", 0)));
    node->InputValues.emplace_back(Value(copyString("", 0)));
    return node;
}



struct ReturnNode : public Node
{
    ReturnNode(int id, const char* name, const BasicFunctionDefPtr& functionDef)
        : Node(id, name, ImColor(255, 255, 255))
    {
        Category = NodeCategory::Return;
        pFunctionDef = functionDef;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        Compiler& compiler = compilerCtx.compiler;

        switch (stage)
        {
        case CompilationStage::BeginInputs:
        {
            GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], InputValues[1]);
            compiler.emitByte(OpByte(OpCode::OP_RETURN));
        }
        break;
        }
    }

    virtual void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        // Add missing inputs
        const int startingInput = 1;

        for (int i = 0; i < pFunctionDef->outputs.size(); ++i)
        {
            const BasicFunctionDef::Input& output = pFunctionDef->outputs[i];

            if (Pin* existingInput = FindInputByName(output.name))
            {
                existingInput->Type = TypeOfValue(output.value);
            }
            else
            {
                Inputs.insert(Inputs.begin() + startingInput + i, { IDGenerator.GetNextId(), output.name.c_str(), TypeOfValue(output.value) });
            }
        }

        // Redo input values
        InputValues.resize(pFunctionDef->inputs.size() + startingInput);

        InputValues[0] = Value(); // Flow node

        for (int i = 0; i < pFunctionDef->inputs.size(); ++i)
        {
            InputValues[i + startingInput] = pFunctionDef->inputs[i].value;
        }

        // Remove outputs that are no longer there
        stl::erase_if(Inputs, [&](const Pin& input)
        {
            return input.Type != PinType::Flow && pFunctionDef->FindOutputByName(input.Name) == nullptr;
        });
    }

    BasicFunctionDefPtr pFunctionDef;
};

static NodePtr BuildReturnNode(IDGenerator& IDGenerator, const ScriptFunction& function)
{
    NodePtr node = std::make_shared<ReturnNode>(IDGenerator.GetNextId(), "Return", function.functionDef);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->InputValues.emplace_back(Value());

    for (int i = 0; i < function.functionDef->outputs.size(); ++i)
    {
        const BasicFunctionDef::Input& output = function.functionDef->outputs[i];
        node->Inputs.emplace_back(IDGenerator.GetNextId(), output.name.c_str(), TypeOfValue(output.value));
        node->InputValues.emplace_back(output.value);
    }

    return node;
}
