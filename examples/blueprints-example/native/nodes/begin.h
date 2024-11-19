
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct BeginNode : public Node
{
    BeginNode(int id, const char* name)
        : Node(id, name, ImColor(255, 255, 255))
    {
        Category = NodeCategory::Begin;
    }

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        // Nothing
    }
};

static NodePtr BuildBeginNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<BeginNode>(IDGenerator.GetNextId(), "Begin");
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    return node;
}

struct GetBoolVariableNode : public Node
{
    GetBoolVariableNode(int id, const char* name)
        : Node(id, name, ImColor(230, 230, 0))
    {
        Category = NodeCategory::Function;
    }

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::PullOutput:
        {
            ObjString* input = asString(InputValues[0]);
            
            const Token outputToken(TokenType::VAR, input->chars.c_str(), input->chars.length(), 0);
            compiler.namedVariable(outputToken, false);

            GraphCompiler::CompileOutput(compiler, graph, Outputs[0]);
        }
        break;
        }
    }
};

static NodePtr GetBoolVariable(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<GetBoolVariableNode>(IDGenerator.GetNextId(), "Check");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Variable", PinType::String);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Value", PinType::Bool);

    node->InputValues.emplace_back(Value(copyString("", 0)));
    return node;
}

struct CreateStringNode : public Node
{
    CreateStringNode(int id, const char* name)
        : Node(id, name, ImColor(230, 230, 0))
    {
        Category = NodeCategory::Function;
    }

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::PullOutput:
        {
            ObjString* input = asString(InputValues[0]);
            compiler.emitConstant(Value(input));

            GraphCompiler::CompileOutput(compiler, graph, Outputs[0]);
        }
        break;
        }
    }
};

static NodePtr CreateString(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<CreateStringNode>(IDGenerator.GetNextId(), "CreateString");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Value", PinType::String);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::String);

    node->InputValues.emplace_back(Value(copyString("", 0)));
    return node;
}


struct AddNode : public Node
{
    AddNode(int id, const char* name)
        : Node(id, name, ImColor(230, 230, 0))
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
        GraphCompiler::CompileInput(compiler, graph, Inputs[0], InputValues[0]);
        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
        compiler.emitByte(OpByte(OpCode::OP_ADD));

        GraphCompiler::CompileOutput(compiler, graph, Outputs[0]);
    }
};

static NodePtr AddNumbers(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<AddNode>(IDGenerator.GetNextId(), "Add");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "A", PinType::Float);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "B", PinType::Float);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Result", PinType::Float);

    node->InputValues.emplace_back(Value(0.0f));
    node->InputValues.emplace_back(Value(0.0f));
    return node;
}

struct ReadFileNode : public Node
{
    ReadFileNode(int id, const char* name)
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
        compiler.namedVariable(Token(TokenType::STRING, "readFile", 8, 10), false);

        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
        
        int argCount = 1;
        compiler.emitBytes(OpByte(OpCode::OP_CALL), argCount);

        GraphCompiler::CompileOutput(compiler, graph, Outputs[1]);
    }
};

static NodePtr CreateReadFileNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<ReadFileNode>(IDGenerator.GetNextId(), "ReadFile");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "File", PinType::String);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Result", PinType::String);

    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(copyString("", 0)));
    return node;
}

struct AppendNode : public Node
{
    AppendNode(int id, const char* name)
        : Node(id, name, ImColor(255, 128, 128))
    {
        Category = NodeCategory::Function;
        Flags |= NodeFlags::DynamicInputs;
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
        GraphCompiler::CompileInput(compiler, graph, Inputs[0], InputValues[0]);

        for (int i = 1; i < Inputs.size(); ++i)
        {
            GraphCompiler::CompileInput(compiler, graph, Inputs[i], InputValues[i]);
            compiler.emitByte(OpByte(OpCode::OP_ADD));
        }

        GraphCompiler::CompileOutput(compiler, graph, Outputs[0]);
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