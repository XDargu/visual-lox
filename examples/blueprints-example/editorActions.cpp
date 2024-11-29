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

    RenameVariableAction::RenameVariableAction(Example* pEditor, int id, const char* name)
    {
        m_pEditor = pEditor;
        m_id = id;
        m_name = name;
    }

    void RenameVariableAction::Run()
    {
        if (ScriptPropertyPtr pVar = ScriptUtils::FindVariableById(m_pEditor->m_script, m_id))
        {
            m_prevName = pVar->Name;
        }
        m_pEditor->RenameVariable(m_id, m_name.c_str());
    }

    void RenameVariableAction::Revert()
    {
        m_pEditor->RenameVariable(m_id, m_prevName.c_str());
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

    DeleteFunctionInputAction::DeleteFunctionInputAction(Example* pEditor, int funId, int inputId, const char* name, const Value& value)
        : m_pEditor(pEditor)
        , m_funId(funId)
        , m_inputId(inputId)
        , m_name(name)
        , m_value(value)
    {
    }

    void DeleteFunctionInputAction::Run()
    {
        m_pEditor->RemoveFunctionInput(m_funId, m_inputId);
    }

    void DeleteFunctionInputAction::Revert()
    {
        m_pEditor->AddFunctionInput(m_funId, m_inputId, m_name.c_str(), m_value);
    }

    void DeleteFunctionInputAction::MarkRoots()
    {
        VM& vm = VM::getInstance();

        vm.markValue(m_value);
    }

    ChangeFunctionInputValueAction::ChangeFunctionInputValueAction(Example* pEditor, int funId, int inputId, Value& value)
        : m_pEditor(pEditor)
        , m_funId(funId)
        , m_inputId(inputId)
        , m_value(value)
    {
    }

    void ChangeFunctionInputValueAction::Run()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_pEditor->m_script, m_funId))
        {
            if (BasicFunctionDef::Input* pInput = pFun->functionDef->FindInputByID(m_inputId))
            {
                m_prevValue = pInput->value;
            }
        }
        m_pEditor->ChangeFunctionInputValue(m_funId, m_inputId, m_value);
    }

    void ChangeFunctionInputValueAction::Revert()
    {
        m_pEditor->ChangeFunctionInputValue(m_funId, m_inputId, m_prevValue);
    }

    void ChangeFunctionInputValueAction::MarkRoots()
    {
        VM& vm = VM::getInstance();

        vm.markValue(m_value);
        vm.markValue(m_prevValue);
    }

    RenameFunctionInputAction::RenameFunctionInputAction(Example* pEditor, int funId, int inputId, const char* name)
        : m_pEditor(pEditor)
        , m_funId(funId)
        , m_inputId(inputId)
        , m_name(name)
    {
    }

    void RenameFunctionInputAction::Run()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_pEditor->m_script, m_funId))
        {
            if (BasicFunctionDef::Input* pInput = pFun->functionDef->FindInputByID(m_inputId))
            {
                m_prevName = pInput->name;
            }
        }
        m_pEditor->RenameFunctionInput(m_funId, m_inputId, m_name.c_str());
    }

    void RenameFunctionInputAction::Revert()
    {
        m_pEditor->RenameFunctionInput(m_funId, m_inputId, m_prevName.c_str());
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

    DeleteFunctionOutputAction::DeleteFunctionOutputAction(Example* pEditor, int funId, int outputId, const char* name, const Value& value)
        : m_pEditor(pEditor)
        , m_funId(funId)
        , m_outputId(outputId)
        , m_name(name)
        , m_value(value)
    {
    }

    void DeleteFunctionOutputAction::Run()
    {
        m_pEditor->RemoveFunctionOutput(m_funId, m_outputId);
    }

    void DeleteFunctionOutputAction::Revert()
    {
        m_pEditor->AddFunctionOutput(m_funId, m_outputId, m_name.c_str(), m_value);
    }

    void DeleteFunctionOutputAction::MarkRoots()
    {
        VM& vm = VM::getInstance();

        vm.markValue(m_value);
    }

    ChangeFunctionOutputValueAction::ChangeFunctionOutputValueAction(Example* pEditor, int funId, int outputId, Value& value)
        : m_pEditor(pEditor)
        , m_funId(funId)
        , m_outputId(outputId)
        , m_value(value)
    {
    }

    void ChangeFunctionOutputValueAction::Run()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_pEditor->m_script, m_funId))
        {
            if (BasicFunctionDef::Input* pOutput = pFun->functionDef->FindOutputByID(m_outputId))
            {
                m_prevValue = pOutput->value;
            }
        }
        m_pEditor->ChangeFunctionOutputValue(m_funId, m_outputId, m_value);
    }

    void ChangeFunctionOutputValueAction::Revert()
    {
        m_pEditor->ChangeFunctionOutputValue(m_funId, m_outputId, m_prevValue);
    }

    void ChangeFunctionOutputValueAction::MarkRoots()
    {
        VM& vm = VM::getInstance();

        vm.markValue(m_value);
        vm.markValue(m_prevValue);
    }

    RenameFunctionOutputAction::RenameFunctionOutputAction(Example* pEditor, int funId, int outputId, const char* name)
        : m_pEditor(pEditor)
        , m_funId(funId)
        , m_outputId(outputId)
        , m_name(name)
    {
    }

    void RenameFunctionOutputAction::Run()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_pEditor->m_script, m_funId))
        {
            if (BasicFunctionDef::Input* pOutput = pFun->functionDef->FindOutputByID(m_outputId))
            {
                m_prevName = pOutput->name;
            }
        }
        m_pEditor->RenameFunctionOutput(m_funId, m_outputId, m_name.c_str());
    }

    void RenameFunctionOutputAction::Revert()
    {
        m_pEditor->RenameFunctionOutput(m_funId, m_outputId, m_prevName.c_str());
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