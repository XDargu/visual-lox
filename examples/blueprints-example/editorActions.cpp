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
        if (ScriptFunction* pFun = ScriptUtils::FindFunctionById(m_pEditor->m_script, m_id))
        {
            m_prevName = pFun->functionDef->name;
        }
        m_pEditor->RenameFunction(m_id, m_name.c_str());
    }

    void RenameFunctionAction::Revert()
    {
        m_pEditor->RenameFunction(m_id, m_prevName.c_str());
    }
}