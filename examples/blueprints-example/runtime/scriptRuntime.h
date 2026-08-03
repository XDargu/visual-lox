#pragma once

#include "../script/script.h"
#include "../validation/scriptValidator.h"
#include "scriptDebugger.h"

#include <Vm.h>

#include <string>
#include <vector>

struct ScriptCompileOptions
{
    bool enableConstantFolding = true;
    bool enableDebugging = false;
    bool disassemble = false;
    std::vector<std::string> programArguments;
};

struct ScriptCompileResult
{
    ObjFunction* function = nullptr;
    InterpretResult status = InterpretResult::INTERPRET_COMPILE_ERROR;
    ValidationReport validation;
    std::vector<Value> foldedValues;
    std::vector<ed::NodeId> foldedNodeIds;
    std::shared_ptr<ScriptDebugInfo> debugInfo;

    explicit operator bool() const
    {
        return status == InterpretResult::INTERPRET_OK && function != nullptr;
    }
};

class ScriptRuntime
{
public:
    static ScriptCompileResult Compile(VM& vm, const Script& script, const ScriptCompileOptions& options = {});
    static InterpretResult Execute(VM& vm, ObjFunction* function);

    static InterpretResult Call(VM& vm, const Value& callable, const std::vector<Value>& arguments = {});
    static InterpretResult CallGlobal(VM& vm, const std::string& name, const std::vector<Value>& arguments = {});

    static InterpretResult Run(VM& vm, const Script& script, const ScriptCompileOptions& options = {});

    static bool HasPausedExecution(VM& vm);
    static InterpretResult Resume(VM& vm);
    static void AbandonPausedExecution(VM& vm);

private:
    static void CompileGraph(const Script& script, const ScriptFunction& function,
                             Compiler& compiler,
                             const std::vector<Value>& foldedValues,
                             const std::vector<ed::NodeId>& foldedNodeIds,
                             ScriptDebugInfo* debugInfo);
};
