#pragma once

#include <application.h>

#include "editorActions.h"

#include "utilities/builders.h"
#include "utilities/widgets.h"

#include "graphs/link.h"
#include "graphs/node.h"
#include "graphs/graphView.h"
#include "graphs/graphCompiler.h"

#include "script/script.h"
#include "script/scriptSerializer.h"
#include "runtime/scriptRuntime.h"
#include "runtime/standardLibrary.h"

#include "graphs/nodeRegistry.h"
#include "operations/documentOperations.h"

#include "utilities/treeview.h"

#include <imgui_node_editor.h>
#include <imgui_internal.h>

#include <Compiler.h>
#include <Vm.h>
#include <Debug.h>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <utility>
#include <sstream>
#include <memory>
#include <cstdint>

namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

using namespace ax;

using ax::Widgets::IconType;

static ed::EditorContext* m_Editor = nullptr;

namespace Editor
{

enum class BottomPanelTab
{
    Problems,
    Output,
    Developer,
};

struct Example :
    public Application
{
    using Application::Application;

    void OnStart() override;
    void OnStop() override;

    void ChangeGraph(const ScriptFunctionPtr& scriptFunction, bool recordHistory = true);
    void NavigateGraphHistory(bool forward);

    void ShowStyleEditor(bool* show = nullptr);
    void ShowNodeSelection(float paneWidth);

    std::vector<ProcessedNode> GatherProcessedNodes(Graph& graph, Compiler& compiler);

    void ShowCompilerInfo(float paneWidth);

    void ShowDebugPanel(float paneWidth);

    void ContextMenu();

    void ShowLeftPane(float paneWidth);
    void ShowScriptExplorer();
    void ShowInspector();
    void ShowBottomPanel();
    void ShowProblemsPanel();
    void ShowOutputPanel();
    void ShowDeveloperPanel();
    void DrawMenuBar();
    void DrawToolbar();
    void DrawStatusBar();
    void HandleShortcuts();
    void CompileScript(bool runAfterCompile);
    void FocusDiagnostic(const ValidationDiagnostic& diagnostic);
    void ApplyEditorTheme();
    void LoadLayoutSettings();
    void SaveLayoutSettings() const;
    void SetBottomPanel(BottomPanelTab tab);

    void OnFrame(float deltaTime) override;
    ImGuiWindowFlags GetWindowFlags() const override;

    void InitializeScriptTree();
    void EnsureMainSignature();
    void RebuildScriptTree();
    void SaveScript(const std::string& path);
    void LoadScript(const std::string& path);
    void ShowFileControls();
    void NewScript();
    void RequestOpenDialog();
    void RequestOpen(const std::string& path);
    void RequestNew();
    void RequestExit();
    void ShowDocumentDialogs();
    void UpdateDocumentState(float deltaTime);
    void RefreshWindowTitle();
    void MarkDocumentSaved();
    void AddRecentFile(const std::string& path);
    void ShowToast(const std::string& message);
    void DrawToasts();
    bool CanClose() override;

    // Tree Node Handling TODO: Move somewhere else
    TreeNode MakeFunctionNode(int funId, const std::string& name);
    TreeNode MakeVariableNode(int varId, const std::string& name);
    TreeNode MakeInputNode(int funId, int inputId, const std::string& name);
    TreeNode MakeOutputNode(int funId, int outputId, const std::string& name);
    TreeNode MakeClassNode(const ScriptClassPtr& scriptClass);
    TreeNode MakeClassMethodNode(int classId, const ScriptFunctionPtr& method);
    TreeNode MakeConstructorNode(int classId, const ScriptFunctionPtr& constructor);
    TreeNode MakeClassPropertyNode(int classId, const ScriptPropertyPtr& property);

    TreeNode* FindNodeByID(int id);
    void EraseNodeByID(int id);

    // Script operations
    void AddFunction(int id);
    void AddFunction(const ScriptFunctionPtr& pExistingFunction);
    void AddVariable(int id);
    void AddVariable(const ScriptPropertyPtr& pVariable);

    void ChangeVariableValue(int id, Value& value);

    void RenameFunction(int funId, const char* name);
    void RenameVariable(int varId, const char* name);

    void AddFunctionInput(int funId, int inputId);
    void AddFunctionInput(int funId, int inputId, const char* name, const Value& value);
    void ChangeFunctionInputValue(int funId, int inputId, Value& value);
    void RenameFunctionInput(int funId, int inputId, const char* name);

    void AddFunctionOutput(int funId, int outputId);
    void AddFunctionOutput(int funId, int outputId, const char* name, const Value& value);
    void ChangeFunctionOutputValue(int funId, int outputId, Value& value);
    void RenameFunctionOutput(int funId, int outputId, const char* name);

    void RemoveFunction(int id);
    void RemoveVariable(int id);

    void RemoveFunctionInput(int funId, int inputId);
    void RemoveFunctionOutput(int funId, int outputId);

    void DoAction(IActionPtr action);
    void UndoLastAction();
    void RedoLastAction();
    bool CanUndo() const;
    bool CanRedo() const;
    void CopySelection();
    void PasteClipboard();

    Script               m_script;
    GraphView            m_graphView;

    ImTextureID          m_HeaderBackground = nullptr;
    ImTextureID          m_SaveIcon = nullptr;
    ImTextureID          m_RestoreIcon = nullptr;
    bool                 m_ShowOrdinals = false;

    // Icons for types of functions
    ImTextureID          m_ScriptIcon = nullptr;
    ImTextureID          m_ClassIcon = nullptr;
    ImTextureID          m_FunctionIcon = nullptr;
    ImTextureID          m_VariableIcon = nullptr;
    ImTextureID          m_InputIcon = nullptr;
    ImTextureID          m_OutputIcon = nullptr;

    IDGenerator          m_IDGenerator;
    NodeRegistry         m_NodeRegistry;
    std::unique_ptr<DocumentOperations> m_operations;

    // Script treeview
    TreeNode             m_scriptTreeView;
    int                  m_selectedItemId = 0;
    int                  m_editingItemId = 0;

    // TODO: Move somewhere else!
    bool m_isConstFoldingEnabled = true;
    bool m_isRealTimeCompilationEnabled = true;
    std::vector<Value>   m_constFoldingValues;
    std::vector<ed::NodeId>   m_constFoldingIDs;

    std::vector<IActionPtr> pendingActions;
    bool m_commitPendingEdit = false;

    std::vector<IActionPtr> actionStack;
    int undoDepth = 0;

    std::string m_currentScriptPath;
    std::string m_fileStatus;
    bool m_fileStatusIsError = false;
    std::string m_savedDocumentSnapshot;
    std::uint64_t m_lastObservedRevision = 0;
    bool m_documentDirty = false;
    float m_autosaveElapsed = 0.0f;
    const std::string m_recoveryPath = ".visual-lox-recovery.vlox";
    bool m_recoveryAvailable = false;
    std::vector<std::string> m_recentFiles;
    std::vector<int> m_graphBackHistory;
    std::vector<int> m_graphForwardHistory;

    enum class PendingDocumentAction { None, New, OpenDialog, Open, Exit, Recover };
    PendingDocumentAction m_pendingDocumentAction = PendingDocumentAction::None;
    std::string m_pendingDocumentPath;
    bool m_openUnsavedDialog = false;
    bool m_allowClose = false;
    std::string m_toastMessage;
    float m_toastTime = 0.0f;
    ValidationReport m_validationReport;

    std::string m_scriptFilter;
    std::string m_compileOutput = "Compile output will appear here.";
    std::string m_runOutput = "Run the script to see its output.";
    bool m_showScriptExplorer = true;
    bool m_showInspector = true;
    bool m_showBottomPanel = true;
    bool m_showDeveloperTools = false;
    bool m_showHelp = false;
    bool m_showAbout = false;
    float m_leftPaneWidth = 290.0f;
    float m_rightPaneWidth = 300.0f;
    float m_bottomPaneHeight = 240.0f;
    BottomPanelTab m_bottomPanelTab = BottomPanelTab::Problems;
    bool m_selectBottomPanelTab = true;
};

}
