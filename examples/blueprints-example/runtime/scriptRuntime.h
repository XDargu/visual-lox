#pragma once

#include "../script/script.h"
#include "../validation/scriptValidator.h"

#include <Vm.h>

#include <vector>

struct ScriptCompileOptions
{
    bool enableConstantFolding = true;
    bool disassemble = false;
};

struct ScriptCompileResult
{
    ObjFunction* function = nullptr;
    InterpretResult status = InterpretResult::INTERPRET_COMPILE_ERROR;
    ValidationReport validation;
    std::vector<Value> foldedValues;
    std::vector<ed::NodeId> foldedNodeIds;

    explicit operator bool() const
    {
        return status == InterpretResult::INTERPRET_OK && function != nullptr;
    }
};

class ScriptRuntime
{
public:
    static ScriptCompileResult Compile(VM& vm, const Script& script,
                                       const ScriptCompileOptions& options = {});
    static InterpretResult Execute(VM& vm, ObjFunction* function);
    static InterpretResult Run(VM& vm, const Script& script,
                               const ScriptCompileOptions& options = {});

private:
    static void CompileGraph(const Graph& graph, Compiler& compiler,
                             const std::vector<Value>& foldedValues,
                             const std::vector<ed::NodeId>& foldedNodeIds);
};
