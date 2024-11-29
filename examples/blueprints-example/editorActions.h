#pragma once

#include <Value.h>

#include <memory>
#include <string>

struct ScriptFunction;
using ScriptFunctionPtr = std::shared_ptr<ScriptFunction>;

struct ScriptProperty;
using ScriptPropertyPtr = std::shared_ptr<ScriptProperty>;

namespace Editor
{
    struct Example;
    struct IAction
    {
        virtual ~IAction() {}

        virtual void Run() = 0;
        virtual void Revert() = 0;
        virtual void MarkRoots() {}
    };

    using IActionPtr = std::shared_ptr<IAction>;

    struct AddFunctionAction : public IAction
    {
        AddFunctionAction(Example* pEditor, int id);

        virtual void Run() override;
        virtual void Revert() override;

        Example* m_pEditor;
        int m_id;
    };

    struct AddVariableAction : public IAction
    {
        AddVariableAction(Example* pEditor, int id);

        virtual void Run() override;
        virtual void Revert() override;

        Example* m_pEditor;
        int m_id;
    };

    struct DeleteVariableAction : public IAction
    {
        DeleteVariableAction(Example* pEditor, const ScriptPropertyPtr& pVariable);

        virtual void Run() override;
        virtual void Revert() override;
        virtual void MarkRoots() override;

        Example* m_pEditor;
        ScriptPropertyPtr m_pVariable;
    };

    struct ChangeVariableValueAction : public IAction
    {
        ChangeVariableValueAction(Example* pEditor, int id, Value& value);

        virtual void Run() override;
        virtual void Revert() override;
        virtual void MarkRoots() override;

        Example* m_pEditor;
        int m_id;
        Value m_value;
        Value m_prevValue;
    };

    struct AddFunctionInputAction : public IAction
    {
        AddFunctionInputAction(Example* pEditor, int funId, int inputId);

        virtual void Run() override;
        virtual void Revert() override;

        Example* m_pEditor;
        int m_funId;
        int m_inputId;
    };

    struct DeleteFunctionInputAction : public IAction
    {
        DeleteFunctionInputAction(Example* pEditor, int funId, int inputId, const char* name, const Value& value);

        virtual void Run() override;
        virtual void Revert() override;
        virtual void MarkRoots() override;

        Example* m_pEditor;
        int m_funId;
        int m_inputId;
        std::string m_name;
        Value m_value;
    };

    struct AddFunctionOutputAction : public IAction
    {
        AddFunctionOutputAction(Example* pEditor, int funId, int inputId);

        virtual void Run() override;
        virtual void Revert() override;

        Example* m_pEditor;
        int m_funId;
        int m_inputId;
    };

    struct DeleteFunctionOutputAction : public IAction
    {
        DeleteFunctionOutputAction(Example* pEditor, int funId, int outputId, const char* name, const Value& value);

        virtual void Run() override;
        virtual void Revert() override;
        virtual void MarkRoots() override;

        Example* m_pEditor;
        int m_funId;
        int m_outputId;
        std::string m_name;
        Value m_value;
    };

    struct RenameFunctionAction : public IAction
    {
        RenameFunctionAction(Example* pEditor, int id, const char* name);

        virtual void Run() override;
        virtual void Revert() override;

        Example* m_pEditor;
        std::string m_name;
        std::string m_prevName;
        int m_id;
    };

    struct DeleteFunctionAction : public IAction
    {
        DeleteFunctionAction(Example* pEditor, const ScriptFunctionPtr& pFunction);

        virtual void Run() override;
        virtual void Revert() override;
        virtual void MarkRoots() override;

        Example* m_pEditor;

        // Keep the function ref active in the action so we can restore it
        // This means we also need to restor the inputs and variables
        ScriptFunctionPtr m_pFunction;
    };
}