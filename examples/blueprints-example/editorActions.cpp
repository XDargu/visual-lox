#include "editorActions.h"

#include "editor.h"
#include "script/script.h"

namespace Editor
{
    AddFunctionAction::AddFunctionAction(Example* pEditor, int id)
    {
        m_pEditor = pEditor;
        m_id = id;
    }

    void AddFunctionAction::Run()
    {
        m_pEditor->AddFunction(m_id);
    }

    void AddFunctionAction::Revert()
    {
        m_pEditor->RemoveFunction(m_id);
    }

    AddVariableAction::AddVariableAction(Example* pEditor, int id)
    {
        m_pEditor = pEditor;
        m_id = id;
    }

    void AddVariableAction::Run()
    {
        m_pEditor->AddVariable(m_id);
    }

    void AddVariableAction::Revert()
    {
        m_pEditor->RemoveVariable(m_id);
    }

    DeleteVariableAction::DeleteVariableAction(Example* pEditor, const ScriptPropertyPtr& pVariable)
        : m_pEditor(pEditor)
        , m_pVariable(pVariable)
    {
    }

    void DeleteVariableAction::Run()
    {
        m_pEditor->RemoveVariable(m_pVariable->ID);
    }

    void DeleteVariableAction::Revert()
    {
        m_pEditor->AddVariable(m_pVariable);
    }

    void DeleteVariableAction::MarkRoots()
    {
        ScriptUtils::MarkVariableRoots(m_pVariable);
    }

    ChangeVariableValueAction::ChangeVariableValueAction(Example* pEditor, int id, Value& value)
        : m_pEditor(pEditor)
        , m_id(id)
        , m_value(value)
    {
    }

    void ChangeVariableValueAction::Run()
    {
        if (ScriptPropertyPtr pVar = ScriptUtils::FindVariableById(m_pEditor->m_script, m_id))
        {
            m_prevValue = pVar->defaultValue;
        }
        m_pEditor->ChangeVariableValue(m_id, m_value);
    }

    void ChangeVariableValueAction::Revert()
    {
        m_pEditor->ChangeVariableValue(m_id, m_prevValue);
    }

    void ChangeVariableValueAction::MarkRoots()
    {
        VM& vm = VM::getInstance();

        vm.markValue(m_value);
        vm.markValue(m_prevValue);
    }

    AddFunctionInputAction::AddFunctionInputAction(Example* pEditor, int funId, int inputId)
    {
        m_pEditor = pEditor;
        m_funId = funId;
        m_inputId = inputId;
    }

    void AddFunctionInputAction::Run()
    {
        m_pEditor->AddFunctionInput(m_funId, m_inputId);
    }

    void AddFunctionInputAction::Revert()
    {
        m_pEditor->RemoveFunctionInput(m_funId, m_inputId);
    }

    AddFunctionOutputAction::AddFunctionOutputAction(Example* pEditor, int funId, int inputId)
    {
        m_pEditor = pEditor;
        m_funId = funId;
        m_inputId = inputId;
    }

    void AddFunctionOutputAction::Run()
    {
        m_pEditor->AddFunctionOutput(m_funId, m_inputId);
    }

    void AddFunctionOutputAction::Revert()
    {
        m_pEditor->RemoveFunctionOutput(m_funId, m_inputId);
    }

    RenameFunctionAction::RenameFunctionAction(Example* pEditor, int id, const char* name)
    {
        m_pEditor = pEditor;
        m_id = id;
        m_name = name;
    }

    void RenameFunctionAction::Run()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_pEditor->m_script, m_id))
        {
            m_prevName = pFun->functionDef->name;
        }
        m_pEditor->RenameFunction(m_id, m_name.c_str());
    }

    void RenameFunctionAction::Revert()
    {
        m_pEditor->RenameFunction(m_id, m_prevName.c_str());
    }

    DeleteFunctionAction::DeleteFunctionAction(Example* pEditor, const ScriptFunctionPtr& pFunction)
    {
        m_pEditor = pEditor;
        m_pFunction = pFunction;
    }

    void DeleteFunctionAction::Run()
    {
        m_pEditor->RemoveFunction(m_pFunction->ID);
    }

    void DeleteFunctionAction::Revert()
    {
        // Add function
        m_pEditor->AddFunction(m_pFunction);
    }

    void DeleteFunctionAction::MarkRoots()
    {
        ScriptUtils::MarkFunctionRoots(m_pFunction);
    }
}