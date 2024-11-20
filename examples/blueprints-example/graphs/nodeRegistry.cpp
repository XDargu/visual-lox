# pragma once

#include "nodeRegistry.h"

#include "graph.h"
#include "graphCompiler.h"

#include <string>
#include <filesystem>

// TODO: Move somewhere else
PinType TypeOfValue(const Value& value)
{
    switch (value.type)
    {
    case ValueType::NIL: return PinType::Any;
    case ValueType::BOOL: return PinType::Bool;
    case ValueType::NUMBER: return PinType::Float;
    case ValueType::OBJ:
    {
        switch (asObject(value)->type)
        {
        case ObjType::STRING: return PinType::String;
        case ObjType::LIST: return PinType::List;
        }
    }
    }

    return PinType::Error;
}

struct NativeFunctionNode : public Node
{
    NativeFunctionNode(int id, const char* name, const NativeFunctionDefPtr& pFunctionDef)
        : Node(id, name, ImColor(255, 128, 128))
        , pFunctionDef(pFunctionDef)
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
        // Gather Inputs normally
        for (int i = 0; i < Inputs.size(); ++i)
        {
            if (Inputs[i].Type != PinType::Flow)
            {
                GraphCompiler::CompileInput(compiler, graph, Inputs[i], InputValues[i]);
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

        // Set the output variable
        const int dataOutputIdx = GraphUtils::IsNodeImplicit(this) ? 0 : 1;
        GraphCompiler::CompileOutput(compiler, graph, Outputs[dataOutputIdx]);
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

    NativeFunctionDefPtr pFunctionDef;
};

NodePtr NativeFunctionDef::MakeNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<NativeFunctionNode>(IDGenerator.GetNextId(), name.c_str(), shared_from_this());

    if (HasFlag(flags, NodeFlags::Implicit))
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

void NodeRegistry::RegisterDefinitions()
{
    definitions.clear();

    RegisterNativeFunc("Square",
        { { "Value", Value(0.0) } },
        { { "Result", Value(0.0) } },
        [](int argCount, Value* args, VM* vm)
        {
            if (isNumber(args[0]))
            {
                double number = asNumber(args[0]);
                return Value(number * number);
            }

            return Value(0.0);
        },
        NodeFlags::Implicit
    );

    RegisterNativeFunc("FileExists",
        { { "File", Value(copyString("", 0))}},
        { { "Exists", Value(false) } },
        [](int argCount, Value* args, VM* vm)
        {
            if (isString(args[0]))
            {
                ObjString* fileName = asString(args[0]);
                return Value(std::filesystem::exists(fileName->chars));
            }

            return Value(false);
        },
        NodeFlags::Implicit
    );

    RegisterNativeFunc("Clock",
        { },
        { { "Time", Value(0.0) } },
        [](int argCount, Value* args, VM* vm)
        {
            return Value((double)clock() / CLOCKS_PER_SEC);
        },
        NodeFlags::Implicit
    );

    RegisterNativeFunc("MakeList",
        { { "List", Value(newList()) } },
        [] (int argCount, Value* args, VM* vm)
        {
            return args[0]; // Result is already a list!
        },
        NodeFlags::Implicit | NodeFlags::DynamicInputs,
        {
            1, 16, PinType::Any, Value(0.0)
        }
    );
}

void NodeRegistry::RegisterNativeFunc(const char* name, std::vector<NativeFunctionDef::Input>&& inputs, std::vector<NativeFunctionDef::Input>&& outputs, NativeFn fun, NodeFlags flags)
{
    NativeFunctionDefPtr nativeFunc  = std::make_shared<NativeFunctionDef>();
    nativeFunc->name = name;

    nativeFunc->inputs = inputs;
    nativeFunc->outputs = outputs;
    nativeFunc->flags = flags;
    nativeFunc->function = fun;

    definitions.push_back(nativeFunc);
}

void NodeRegistry::RegisterNativeFunc(const char* name, std::vector<NativeFunctionDef::Input>&& outputs, NativeFn fun, NodeFlags flags, NativeFunctionDef::DynamicInputProps&& dynamicProps)
{
    NativeFunctionDefPtr nativeFunc = std::make_shared<NativeFunctionDef>();
    nativeFunc->name = name;

    nativeFunc->inputs = { { "Dumy", Value(2.0) }}; // Single input, input will be a list!
    nativeFunc->outputs = outputs;
    nativeFunc->flags = flags;
    nativeFunc->function = fun;
    nativeFunc->dynamicInputProps = dynamicProps;

    definitions.push_back(nativeFunc);
}

void NodeRegistry::RegisterNatives(VM& vm)
{
    for (NativeFunctionDefPtr& def : definitions)
    {
        vm.defineNative(def->name.c_str(), def->inputs.size(), def->function);
    }
}
