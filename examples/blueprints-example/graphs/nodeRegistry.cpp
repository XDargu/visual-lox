# pragma once

#include "nodeRegistry.h"

#include "graph.h"
#include "graphCompiler.h"

#include <string>

struct NativeFunctionNode : public Node
{
    NativeFunctionNode(int id, const char* name)
        : Node(id, name, ImColor(255, 128, 128))
    {
        Category = NodeCategory::Function;
    }

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInput:
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
        // Load named variable (native func)
        compiler.namedVariable(Token(TokenType::STRING, Name.c_str(), Name.length(), 10), false);

        int argCount = 0;
        for (int i = 0; i < Inputs.size(); ++i)
        {
            if (Inputs[i].Type != PinType::Flow)
            {
                GraphCompiler::CompileInput(compiler, graph, Inputs[i], InputValues[i]);
                argCount++;
            }
        }

        compiler.emitBytes(OpByte(OpCode::OP_CALL), argCount);

        // Set the output variable
        const int dataOutputIdx = GraphUtils::IsNodeImplicit(this) ? 0 : 1;
        GraphCompiler::CompileOutput(compiler, Outputs[dataOutputIdx]);
    }
};

PinType TypeOfValue(const Value& value)
{
    switch (value.type)
    {
    case ValueType::BOOL: return PinType::Bool;
    case ValueType::NUMBER: return PinType::Float;
    case ValueType::OBJ:
    {
        switch (asObject(value)->type)
        {
            case ObjType::STRING: return PinType::String;
        }
    }
    }

    return PinType::Object;
}

NodePtr NativeFunctionDef::MakeNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<NativeFunctionNode>(IDGenerator.GetNextId(), name.c_str());

    if (!isImplicit)
    {
        node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
        node->InputValues.emplace_back(Value());
        
        node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    }

    for (const Input& input : inputs)
    {
        node->Inputs.emplace_back(IDGenerator.GetNextId(), input.name.c_str(), TypeOfValue(input.value));
        node->InputValues.emplace_back(input.value);
    }

    for (const Input& output : outputs)
    {
        node->Outputs.emplace_back(IDGenerator.GetNextId(), output.name.c_str(), TypeOfValue(output.value));
    }

    return node;
}

void NodeRegistry::RegisterDefinitions()
{
    definitions.clear();

    // TODO: Move somewhere else
    NativeFunctionDef square;
    square.name = "Square";

    square.inputs.push_back({ "Value", Value(0.0) });
    square.outputs.push_back({ "Result", Value(0.0) });
    square.isImplicit = false;

    square.function = [](int argCount, Value* args, VM* vm)
    {
        if (isNumber(args[0]))
        {
            double number = asNumber(args[0]);
            return Value(number * number);
        }

        return Value(0.0);
    };

    definitions.push_back(square);
}

void NodeRegistry::RegisterNatives(VM& vm)
{
    for (NativeFunctionDef& def : definitions)
    {
        vm.defineNative(def.name.c_str(), def.inputs.size(), def.function);
    }
}
