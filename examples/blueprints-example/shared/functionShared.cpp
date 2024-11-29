
# pragma once

#include "functionShared.h"

#include "../graphs/node.h"
#include "../graphs/graph.h"
#include "../graphs/idgeneration.h"
#include "../graphs/graphCompiler.h"

#include "../utilities/utils.h"

#include "../script/script.h"

#include <Compiler.h>


struct FunctionNode : public Node
{
    FunctionNode(int id, const char* name, const BasicFunctionDefPtr& pFunctionDef, ScriptElementID funcID)
        : Node(id, name, ImColor(255, 128, 128))
        , pFunctionDef(pFunctionDef)
    {
        refId = funcID;
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

    virtual void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        Flags = ClearFlag(Flags, NodeFlags::Error);

        RefreshDefinition(script);

        if (!pFunctionDef)
        {
            Flags |= NodeFlags::Error;
            Error = "Missing function with ID: " + std::to_string(refId);
            return;
        }

        // Basic info
        Name = pFunctionDef->name;

        // Add missing inputs
        int startingInput = HasFlag(Flags, NodeFlags::ReadOnly) ? 0 : 1;

        for (int i = 0; i < pFunctionDef->inputs.size(); ++i)
        {
            const BasicFunctionDef::Input& input = pFunctionDef->inputs[i];

            if (Pin* existingInput = FindInputByName(input.name))
            {
                existingInput->Type = TypeOfValue(input.value);
                auto it = InputValues.begin() + startingInput + i;
            }
            else
            {
                Inputs.insert(Inputs.begin() + startingInput + i, { IDGenerator.GetNextId(), input.name.c_str(), TypeOfValue(input.value) });
            }
        }

        // Remove inputs that are no longer there
        stl::erase_if(Inputs, [&](const Pin& input)
        {
            return input.Type != PinType::Flow && pFunctionDef->FindInputByName(input.Name) == nullptr;
        });

        // Redo input values
        InputValues.resize(pFunctionDef->inputs.size() + startingInput);

        if (startingInput == 1)
            InputValues[0] = Value(); // Flow node

        for (int i = 0; i < pFunctionDef->inputs.size(); ++i)
        {
            InputValues[i + startingInput] = pFunctionDef->inputs[i].value;
        }

        // Add missing outputs
        int startingOutput = HasFlag(Flags, NodeFlags::ReadOnly) ? 0 : 1;

        for (int i = 0; i < pFunctionDef->outputs.size(); ++i)
        {
            const BasicFunctionDef::Input& output = pFunctionDef->outputs[i];

            if (Pin* existingOutput = FindOutputByName(output.name))
            {
                existingOutput->Type = TypeOfValue(output.value);
            }
            else
            {
                Outputs.insert(Outputs.begin() + startingOutput + i, { IDGenerator.GetNextId(), output.name.c_str(), TypeOfValue(output.value) });
            }
        }

        // Remove outputs that are no longer there
        stl::erase_if(Outputs, [&](const Pin& output)
        {
            return output.Type != PinType::Flow && pFunctionDef->FindOutputByName(output.Name) == nullptr;
        });
    }

    void RefreshDefinition(const Script& script)
    {
        const bool isNative = !refId.IsValid();

        if (!isNative)
        {
            if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(script, refId))
            {
                pFunctionDef = pFun->functionDef;
            }
            else
            {
                pFunctionDef = nullptr;
            }
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

NodePtr BasicFunctionDef::MakeNode(IDGenerator& IDGenerator, ScriptElementID funcID)
{
    NodePtr node = std::make_shared<FunctionNode>(IDGenerator.GetNextId(), name.c_str(), shared_from_this(), funcID);

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

BasicFunctionDef::Input* BasicFunctionDef::FindOutputByName(const std::string& name)
{
    for (Input& input : outputs)
    {
        if (input.name == name)
        {
            return &input;
        }
    }

    return nullptr;
}

BasicFunctionDef::Input* BasicFunctionDef::FindInputByName(const std::string& name)
{
    for (Input& input : inputs)
    {
        if (input.name == name)
        {
            return &input;
        }
    }

    return nullptr;
}

BasicFunctionDef::Input* BasicFunctionDef::FindOutputByID(const int inputId)
{
    for (Input& input : outputs)
    {
        if (input.id == inputId)
        {
            return &input;
        }
    }

    return nullptr;
}

BasicFunctionDef::Input* BasicFunctionDef::FindInputByID(const int inputId)
{
    for (Input& input : inputs)
    {
        if (input.id == inputId)
        {
            return &input;
        }
    }

    return nullptr;
}
