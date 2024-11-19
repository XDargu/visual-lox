
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

struct BranchNode : public Node
{
    BranchNode(int id, const char* name)
        : Node(id, name, ImColor(255, 255, 255))
    {
        Category = NodeCategory::Branch;
    }

    // TODO: Come up with a better way of doing this!
    mutable size_t thenJump = 0;
    mutable size_t elseJump = 0;

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
            case CompilationStage::BeginInput:
            {
                CompileInputs(compiler, graph);
            }
            break;
            case CompilationStage::BeginOutput:
            {
                if (portIdx == 0) // TRUE
                {
                    thenJump = compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
                    compiler.emitByte(OpByte(OpCode::OP_POP));
                }
                else if (portIdx == 1) // FALSe
                {
                    elseJump = compiler.emitJump(OpByte(OpCode::OP_JUMP));
                    compiler.patchJump(thenJump);
                    compiler.emitByte(OpByte(OpCode::OP_POP));
                }
            }
            break;
            case CompilationStage::EndInput:
            {
                compiler.patchJump(elseJump);
            }
            break;
        }
    }

    void CompileInputs(Compiler& compiler, const Graph& graph) const
    {
        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
    }
};

static NodePtr BuildBranchNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<BranchNode>(IDGenerator.GetNextId(), "Branch");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Condition", PinType::Bool);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "True", PinType::Flow);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "False", PinType::Flow);

    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(false));
    return node;
}

struct PrintNode : public Node
{
    PrintNode(int id, const char* name)
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
        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
        compiler.emitByte(OpByte(OpCode::OP_PRINT));
    }
};

static NodePtr BuildPrintNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<PrintNode>(IDGenerator.GetNextId(), "Print");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Content", PinType::Any);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);

    // TODO: These values will be garbage collected! We will need to mark them somewhow
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(copyString("", 0)));
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