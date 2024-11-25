#include "editorActions.h"

#include "editor.h"

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
        //m_pEditor->RemoveVariable(m_id);
    }

}