#pragma once

#include <memory>
#include <string>

namespace Editor
{
    struct Example;
    struct IAction
    {
        virtual ~IAction() {}

        virtual void Run() = 0;
        virtual void Revert() = 0;
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

    struct AddFunctionInputAction : public IAction
    {
        AddFunctionInputAction(Example* pEditor, int funId, int inputId);

        virtual void Run() override;
        virtual void Revert() override;

        Example* m_pEditor;
        int m_funId;
        int m_inputId;
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

}