#include "editor.h"

#include "native/nodes/begin.h"

#include "utilities/utils.h"

#include <filesystem>
#include <optional>
#include <stack>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

namespace Editor
{

namespace
{
std::optional<std::string> SelectVloxFile(bool save, const std::string& currentPath)
{
#ifdef _WIN32
    char pathBuffer[4096] = {};
    if (!currentPath.empty())
        strncpy_s(pathBuffer, currentPath.c_str(), _TRUNCATE);

    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "Visual Lox scripts (*.vlox)\0*.vlox\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = pathBuffer;
    dialog.nMaxFile = static_cast<DWORD>(sizeof(pathBuffer));
    dialog.lpstrDefExt = "vlox";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (save)
        dialog.Flags |= OFN_OVERWRITEPROMPT;
    else
        dialog.Flags |= OFN_FILEMUSTEXIST;

    const BOOL selected = save ? GetSaveFileNameA(&dialog) : GetOpenFileNameA(&dialog);
    if (selected)
        return std::string(pathBuffer);
#else
    (void)save;
    (void)currentPath;
#endif
    return std::nullopt;
}
}

static bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f)
{
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID("##Splitter");
    ImRect bb;
    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f);
}

namespace ImGuiUtils
{
    void BeginDisabled(bool disabled)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * disabled ? 0.5f : 1.0f);
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, disabled);
    }

    void EndDisabled()
    {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }
}

namespace Utils
{
    static void DrawEachLine(const std::string& text)
    {
        std::stringstream stream(text);
        std::string segment;

        while (std::getline(stream, segment, '\n'))
        {
            ImGui::Text(segment.c_str());
        }
    }

    std::string FindValidName(const char* name, const TreeNode& scope)
    {
        std::string nextName = name;

        int sufix = 0;
        bool found = false;

        while (!found)
        {
            found = true;
            for (auto& node : scope.children)
            {
                if (node.label == nextName)
                {
                    sufix++;
                    nextName = name + std::to_string(sufix);
                    found = false;
                    break;
                }
            }
        }

        return nextName;
    }

    struct CaptureStdout
    {
        CaptureStdout()
        {
            // Redirect cout.
            oldCoutStreamBuf = std::cout.rdbuf();
            std::cout.rdbuf(strCout.rdbuf());

            // Redirect cerr.
            oldCerrStreamBuf = std::cerr.rdbuf();
            std::cerr.rdbuf(strCout.rdbuf());
        }

        std::string Restore()
        {
            // Restore old cout.
            std::cout.rdbuf(oldCoutStreamBuf);
            std::cerr.rdbuf(oldCerrStreamBuf);

            return strCout.str();
        }

        std::streambuf* oldCoutStreamBuf;
        std::streambuf* oldCerrStreamBuf;
        std::ostringstream strCout;
    };
}

void Example::OnStart()
{
    m_graphView.setIDGenerator(m_IDGenerator);
    m_graphView.Init(LargeNodeFont());
    m_graphView.setNodeRegistry(m_NodeRegistry);

    m_HeaderBackground = LoadTexture("data/BlueprintBackground.png");
    m_SaveIcon = LoadTexture("data/ic_save_white_24dp.png");
    m_RestoreIcon = LoadTexture("data/ic_restore_white_24dp.png");

    m_ScriptIcon = LoadTexture("data/ic_script.png");
    m_ClassIcon = LoadTexture("data/ic_class.png");
    m_FunctionIcon = LoadTexture("data/ic_function.png");
    m_VariableIcon = LoadTexture("data/ic_variable.png");
    m_InputIcon = LoadTexture("data/ic_input.png");
    m_OutputIcon = LoadTexture("data/ic_output.png");

    VM& vm = VM::getInstance();
    vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(m_NodeRegistry, vm);

        for (Value& value : m_constFoldingValues)
        {
            vm.markValue(value);
        }

        ScriptUtils::MarkScriptRoots(m_script);

        for (const IActionPtr& pAction : pendingActions)
        {
            pAction->MarkRoots();
        }

        for (const IActionPtr& pAction : actionStack)
        {
            pAction->MarkRoots();
        }
    });

    RegisterStandardLibrary(m_NodeRegistry);
    m_NodeRegistry.RegisterNatives(vm);

    // Script ID
    m_script.ID = m_IDGenerator.GetNextId();

    // Add begin to main function
    m_script.main = std::make_shared<ScriptFunction>(m_IDGenerator.GetNextId(), "Main");

    // Start with main graph
    m_graphView.SetGraph(&m_script, m_script.main, &m_script.main->Graph);

    NodePtr beginMain = BuildBeginNode(m_IDGenerator, m_script.main);
    NodeUtils::BuildNode(beginMain);
    m_script.main->Graph.AddNode(beginMain);

    RebuildScriptTree();
}

void Example::OnStop()
{
    auto releaseTexture = [this](ImTextureID& id)
    {
        if (id)
        {
            DestroyTexture(id);
            id = nullptr;
        }
    };

    releaseTexture(m_RestoreIcon);
    releaseTexture(m_SaveIcon);
    releaseTexture(m_HeaderBackground);

    releaseTexture(m_ScriptIcon);
    releaseTexture(m_ClassIcon);
    releaseTexture(m_FunctionIcon);
    releaseTexture(m_VariableIcon);
    releaseTexture(m_InputIcon);
    releaseTexture(m_OutputIcon);

}

void Example::ChangeGraph(const ScriptFunctionPtr& scriptFunction)
{
    m_graphView.SetGraph(&m_script, scriptFunction, &scriptFunction->Graph);
}

void Example::ShowStyleEditor(bool* show)
{
    if (!ImGui::Begin("Style", show))
    {
        ImGui::End();
        return;
    }

    auto paneWidth = ImGui::GetContentRegionAvail().x;

    auto& editorStyle = ed::GetStyle();
    ImGui::BeginHorizontal("Style buttons", ImVec2(paneWidth, 0), 1.0f);
    ImGui::TextUnformatted("Values");
    ImGui::Spring();
    if (ImGui::Button("Reset to defaults"))
        editorStyle = ed::Style();
    ImGui::EndHorizontal();
    ImGui::Spacing();
    ImGui::DragFloat4("Node Padding", &editorStyle.NodePadding.x, 0.1f, 0.0f, 40.0f);
    ImGui::DragFloat("Node Rounding", &editorStyle.NodeRounding, 0.1f, 0.0f, 40.0f);
    ImGui::DragFloat("Node Border Width", &editorStyle.NodeBorderWidth, 0.1f, 0.0f, 15.0f);
    ImGui::DragFloat("Hovered Node Border Width", &editorStyle.HoveredNodeBorderWidth, 0.1f, 0.0f, 15.0f);
    ImGui::DragFloat("Hovered Node Border Offset", &editorStyle.HoverNodeBorderOffset, 0.1f, -40.0f, 40.0f);
    ImGui::DragFloat("Selected Node Border Width", &editorStyle.SelectedNodeBorderWidth, 0.1f, 0.0f, 15.0f);
    ImGui::DragFloat("Selected Node Border Offset", &editorStyle.SelectedNodeBorderOffset, 0.1f, -40.0f, 40.0f);
    ImGui::DragFloat("Pin Rounding", &editorStyle.PinRounding, 0.1f, 0.0f, 40.0f);
    ImGui::DragFloat("Pin Border Width", &editorStyle.PinBorderWidth, 0.1f, 0.0f, 15.0f);
    ImGui::DragFloat("Link Strength", &editorStyle.LinkStrength, 1.0f, 0.0f, 500.0f);
    //ImVec2  SourceDirection;
    //ImVec2  TargetDirection;
    ImGui::DragFloat("Scroll Duration", &editorStyle.ScrollDuration, 0.001f, 0.0f, 2.0f);
    ImGui::DragFloat("Flow Marker Distance", &editorStyle.FlowMarkerDistance, 1.0f, 1.0f, 200.0f);
    ImGui::DragFloat("Flow Speed", &editorStyle.FlowSpeed, 1.0f, 1.0f, 2000.0f);
    ImGui::DragFloat("Flow Duration", &editorStyle.FlowDuration, 0.001f, 0.0f, 5.0f);
    //ImVec2  PivotAlignment;
    //ImVec2  PivotSize;
    //ImVec2  PivotScale;
    //float   PinCorners;
    //float   PinRadius;
    //float   PinArrowSize;
    //float   PinArrowWidth;
    ImGui::DragFloat("Group Rounding", &editorStyle.GroupRounding, 0.1f, 0.0f, 40.0f);
    ImGui::DragFloat("Group Border Width", &editorStyle.GroupBorderWidth, 0.1f, 0.0f, 15.0f);

    ImGui::Separator();

    static ImGuiColorEditFlags edit_mode = ImGuiColorEditFlags_DisplayRGB;
    ImGui::BeginHorizontal("Color Mode", ImVec2(paneWidth, 0), 1.0f);
    ImGui::TextUnformatted("Filter Colors");
    ImGui::Spring();
    ImGui::RadioButton("RGB", &edit_mode, ImGuiColorEditFlags_DisplayRGB);
    ImGui::Spring(0);
    ImGui::RadioButton("HSV", &edit_mode, ImGuiColorEditFlags_DisplayHSV);
    ImGui::Spring(0);
    ImGui::RadioButton("HEX", &edit_mode, ImGuiColorEditFlags_DisplayHex);
    ImGui::EndHorizontal();

    static ImGuiTextFilter filter;
    filter.Draw("##filter", paneWidth);

    ImGui::Spacing();

    ImGui::PushItemWidth(-160);
    for (int i = 0; i < ed::StyleColor_Count; ++i)
    {
        auto name = ed::GetStyleColorName((ed::StyleColor)i);
        if (!filter.PassFilter(name))
            continue;

        ImGui::ColorEdit4(name, &editorStyle.Colors[i].x, edit_mode);
    }
    ImGui::PopItemWidth();

    ImGui::End();
}

void Example::ShowNodeSelection(float paneWidth)
{
    auto& io = ImGui::GetIO();

    std::vector<ed::NodeId> selectedNodes;
    std::vector<ed::LinkId> selectedLinks;
    selectedNodes.resize(ed::GetSelectedObjectCount());
    selectedLinks.resize(ed::GetSelectedObjectCount());

    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
    int linkCount = ed::GetSelectedLinks(selectedLinks.data(), static_cast<int>(selectedLinks.size()));

    selectedNodes.resize(nodeCount);
    selectedLinks.resize(linkCount);

    int saveIconWidth = GetTextureWidth(m_SaveIcon);
    int saveIconHeight = GetTextureWidth(m_SaveIcon);
    int restoreIconWidth = GetTextureWidth(m_RestoreIcon);
    int restoreIconHeight = GetTextureWidth(m_RestoreIcon);

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetCursorScreenPos(),
        ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
        ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f);
    ImGui::Spacing(); ImGui::SameLine();
    ImGui::TextUnformatted("Nodes");
    ImGui::Indent();
    for (auto& node : m_graphView.m_pGraph->GetNodes())
    {
        ImGui::PushID(node->ID.AsPointer());
        auto start = ImGui::GetCursorScreenPos();

        if (const auto progress = m_graphView.GetTouchProgress(node->ID))
        {
            ImGui::GetWindowDrawList()->AddLine(
                start + ImVec2(-8, 0),
                start + ImVec2(-8, ImGui::GetTextLineHeight()),
                IM_COL32(255, 0, 0, 255 - (int)(255 * progress)), 4.0f);
        }

        bool isSelected = std::find(selectedNodes.begin(), selectedNodes.end(), node->ID) != selectedNodes.end();
# if IMGUI_VERSION_NUM >= 18967
        ImGui::SetNextItemAllowOverlap();
# endif
        if (ImGui::Selectable((node->Name + "##" + std::to_string(reinterpret_cast<uintptr_t>(node->ID.AsPointer()))).c_str(), &isSelected))
        {
            if (io.KeyCtrl)
            {
                if (isSelected)
                    ed::SelectNode(node->ID, true);
                else
                    ed::DeselectNode(node->ID);
            }
            else
                ed::SelectNode(node->ID, false);

            ed::NavigateToSelection();
        }
        if (ImGui::IsItemHovered() && !node->State.empty())
            ImGui::SetTooltip("State: %s", node->State.c_str());

        auto id = std::string("(") + std::to_string(reinterpret_cast<uintptr_t>(node->ID.AsPointer())) + ")";
        auto textSize = ImGui::CalcTextSize(id.c_str(), nullptr);
        auto iconPanelPos = start + ImVec2(
            paneWidth - ImGui::GetStyle().FramePadding.x - ImGui::GetStyle().IndentSpacing - saveIconWidth - restoreIconWidth - ImGui::GetStyle().ItemInnerSpacing.x * 1,
            (ImGui::GetTextLineHeight() - saveIconHeight) / 2);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(iconPanelPos.x - textSize.x - ImGui::GetStyle().ItemInnerSpacing.x, start.y),
            IM_COL32(255, 255, 255, 255), id.c_str(), nullptr);

        auto drawList = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos(iconPanelPos);
# if IMGUI_VERSION_NUM < 18967
        ImGui::SetItemAllowOverlap();
# else
        ImGui::SetNextItemAllowOverlap();
# endif
        if (node->SavedState.empty())
        {
            if (ImGui::InvisibleButton("save", ImVec2((float)saveIconWidth, (float)saveIconHeight)))
                node->SavedState = node->State;

            if (ImGui::IsItemActive())
                drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 96));
            else if (ImGui::IsItemHovered())
                drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
            else
                drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 160));
        }
        else
        {
            ImGui::Dummy(ImVec2((float)saveIconWidth, (float)saveIconHeight));
            drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 32));
        }

        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
# if IMGUI_VERSION_NUM < 18967
        ImGui::SetItemAllowOverlap();
# else
        ImGui::SetNextItemAllowOverlap();
# endif
        if (!node->SavedState.empty())
        {
            if (ImGui::InvisibleButton("restore", ImVec2((float)restoreIconWidth, (float)restoreIconHeight)))
            {
                node->State = node->SavedState;
                ed::RestoreNodeState(node->ID);
                node->SavedState.clear();
            }

            if (ImGui::IsItemActive())
                drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 96));
            else if (ImGui::IsItemHovered())
                drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
            else
                drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 160));
        }
        else
        {
            ImGui::Dummy(ImVec2((float)restoreIconWidth, (float)restoreIconHeight));
            drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 32));
        }

        ImGui::SameLine(0, 0);
# if IMGUI_VERSION_NUM < 18967
        ImGui::SetItemAllowOverlap();
# endif
        ImGui::Dummy(ImVec2(0, (float)restoreIconHeight));

        ImGui::PopID();
    }
    ImGui::Unindent();

    static int changeCount = 0;

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetCursorScreenPos(),
        ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
        ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f);
    ImGui::Spacing(); ImGui::SameLine();
    ImGui::TextUnformatted("Selection");

    ImGui::BeginHorizontal("Selection Stats", ImVec2(paneWidth, 0));
    ImGui::Text("Changed %d time%s", changeCount, changeCount > 1 ? "s" : "");
    ImGui::Spring();
    if (ImGui::Button("Deselect All"))
        ed::ClearSelection();
    ImGui::EndHorizontal();
    ImGui::Indent();
    for (int i = 0; i < nodeCount; ++i) ImGui::Text("Node (%p)", selectedNodes[i].AsPointer());
    for (int i = 0; i < linkCount; ++i) ImGui::Text("Link (%p)", selectedLinks[i].AsPointer());
    ImGui::Unindent();

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z)))
        for (auto& link : m_graphView.m_pGraph->GetLinks())
            ed::Flow(link.ID);

    if (ed::HasSelectionChanged())
        ++changeCount;
}

std::vector<ProcessedNode> Example::GatherProcessedNodes(Graph& graph, Compiler& compiler)
{
    std::vector<ProcessedNode> processedNodes;
    std::vector<int> stackFrames;

    NodePtr begin = graph.FindNodeIf([](const NodePtr& node) { return node->Category == NodeCategory::Begin; });
    if (begin)
    {
        GraphCompiler graphCompiler(compiler);

        int currentStackFrame = 0;
        stackFrames.push_back(currentStackFrame);

        graphCompiler.CompileGraph(graph, begin, 0, [&](const NodePtr& node, const Graph& graph, CompilationStage stage, int portIdx)
        {
            if (!GraphUtils::IsNodeImplicit(node))
            {
                if (stage == CompilationStage::BeginInputs)
                {
                    auto result = std::find_if(processedNodes.begin(), processedNodes.end(), [&](const ProcessedNode& pnode) { return pnode.node->ID == node->ID; });
                    if (result == processedNodes.end())
                    {
                        ProcessedNode pnode;
                        pnode.node = node;
                        pnode.stackFrames = stackFrames;
                        processedNodes.push_back(pnode);
                    }
                    else
                    {
                        for (int stackFrame : stackFrames)
                        {
                            if (std::find(result->stackFrames.begin(), result->stackFrames.end(), stackFrame) == result->stackFrames.end())
                                result->stackFrames.push_back(stackFrame);
                        }
                    }
                }
                else if (stage == CompilationStage::BeginOutput)
                {
                    int flowCount = 0;
                    for (auto& output : node->Outputs)
                    {
                        if (output.Type == PinType::Flow)
                            flowCount++;
                    }

                    if (flowCount > 1)
                    {
                        ++currentStackFrame;
                        stackFrames.push_back(currentStackFrame);
                    }
                }
                else if (stage == CompilationStage::EndOutput)
                {
                    int flowCount = 0;
                    for (auto& output : node->Outputs)
                    {
                        if (output.Type == PinType::Flow)
                            flowCount++;
                    }

                    if (flowCount > 1)
                    {
                        stackFrames.pop_back();
                    }
                }
            }
            else
            {
                if (stage == CompilationStage::PullOutput)
                {
                    if (std::find_if(processedNodes.begin(), processedNodes.end(), [&](const ProcessedNode& pnode) { return pnode.node->ID == node->ID; }) == processedNodes.end())
                    {
                        ProcessedNode pnode;
                        pnode.node = node;
                        processedNodes.push_back(pnode);
                    }
                }
            }
            
        });
    }

    return processedNodes;
}

void Example::ShowCompilerInfo(float paneWidth)
{
    static std::string result = "<output>";
    static std::string runResult = "";

    ImGui::Text("Validation: %zu error(s), %zu warning(s)",
                m_validationReport.ErrorCount(), m_validationReport.WarningCount());
    for (const ValidationDiagnostic& diagnostic : m_validationReport.diagnostics)
    {
        const ImVec4 color = diagnostic.severity == DiagnosticSeverity::Error
            ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
            : ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", FormatDiagnostic(diagnostic).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    ImGui::Checkbox("Const Folding Enabled", &m_isConstFoldingEnabled);
    ImGui::Checkbox("Real time compilation Enabled", &m_isRealTimeCompilationEnabled);

    VM& vm = VM::getInstance();

    const bool pressedCompile = ImGui::Button("Compile") || m_isRealTimeCompilationEnabled;
    const bool pressedRun = ImGui::Button("Run");

    if (pressedCompile || pressedRun)
    {
        Utils::CaptureStdout captureCompilation;

        std::cout << std::endl << "Compiling script: " << std::endl;
        static ObjFunction* function = nullptr;
        ScriptCompileOptions compileOptions;
        compileOptions.enableConstantFolding = m_isConstFoldingEnabled;
        compileOptions.disassemble = true;
        const ScriptCompileResult compileResult = ScriptRuntime::Compile(vm, m_script, compileOptions);
        m_validationReport = compileResult.validation;
        m_constFoldingValues = compileResult.foldedValues;
        m_constFoldingIDs = compileResult.foldedNodeIds;
        function = compileResult.function;

        result = captureCompilation.Restore();

        if (pressedRun)
        {
            if (function != nullptr)
            {
                Utils::CaptureStdout captureExecution;
                const InterpretResult vmResult = ScriptRuntime::Execute(vm, function);

                if (vmResult == InterpretResult::INTERPRET_COMPILE_ERROR)
                    std::cout << "Compilation Error";
                else if (vmResult == InterpretResult::INTERPRET_RUNTIME_ERROR)
                    std::cout << "Runtime Error";

                runResult = "Execution output:\n" + captureExecution.Restore();
            }
        }
    }



    /*ImGui::Text("Ctrl %s", ImGui::GetIO().KeyCtrl ? "true" : "false");
    ImGui::Text("Alt %s", ImGui::GetIO().KeyAlt ? "true" : "false");
    ImGui::Text("Shift %s", ImGui::GetIO().KeyShift ? "true" : "false");

    for (int i = ImGuiKey_None; i < ImGuiKey_COUNT; ++i)
    {
    ImGui::Text("%d %s", i, ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? "true" : "false");
    }*/


    Utils::DrawEachLine(result);
    //Utils::DrawEachLine(runResult);

    static bool runResultOpen = true;

    if (pressedRun)
    {
        runResultOpen = true;
        ImGui::OpenPopup("Run Result");
    }

    if (ImGui::BeginPopupModal("Run Result", &runResultOpen))
    {
        Utils::DrawEachLine(runResult);
        ImGui::EndPopup();
    }


    {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
            ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f);
        ImGui::Spacing(); ImGui::SameLine();
        ImGui::TextUnformatted("StringTable");
        ImGui::Indent();

        VM& vm = VM::getInstance();
        const Table& table = vm.stringTable();
        size_t entries = table.getEntriesSize();
        for (size_t i = 0; i < entries; ++i)
        {
            if (const Entry* entry = table.getEntryByIndex(i))
            {
                if (entry->key)
                {
                    ImGui::Text(entry->key->chars.c_str());
                }
            }
        }

        ImGui::Unindent();
    }

    {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
            ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f);
        ImGui::Spacing(); ImGui::SameLine();
        ImGui::TextUnformatted("Globals");
        ImGui::Indent();

        VM& vm = VM::getInstance();
        const Table& table = vm.globalTable();
        size_t entries = table.getEntriesSize();
        for (size_t i = 0; i < entries; ++i)
        {
            if (const Entry* entry = table.getEntryByIndex(i))
            {
                if (entry->key)
                {
                    ImGui::Text(entry->key->chars.c_str());

                    ImGui::SameLine();

                    ImGui::Text(valueAsStr(entry->value).c_str());
                }
            }
        }

        ImGui::Unindent();
    }

    {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
            ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f);
        ImGui::Spacing(); ImGui::SameLine();
        ImGui::TextUnformatted("Folded nodes");
        ImGui::Indent();

        VM& vm = VM::getInstance();
        size_t entries = m_constFoldingIDs.size();

        for (size_t i = 0; i < entries; ++i)
        {
            const Value& value = m_constFoldingValues[i];
            const ed::NodeId& nodeId = m_constFoldingIDs[i];

            ImGui::Text("%p", nodeId.AsPointer());

            ImGui::SameLine();

            ImGui::Text(valueAsStr(value).c_str());
        }

        ImGui::Unindent();
    }
}

void Example::ShowDebugPanel(float paneWidth)
{
    static bool showStyleEditor = false;
    ImGui::BeginHorizontal("Style Editor", ImVec2(paneWidth, 0));
    ImGui::Spring(0.0f, 0.0f);
    if (ImGui::Button("Zoom to Content"))
        ed::NavigateToContent();
    ImGui::Spring(0.0f);
    if (ImGui::Button("Show Flow"))
    {
        for (auto& link : m_graphView.m_pGraph->GetLinks())
            ed::Flow(link.ID);
    }
    ImGui::Spring();
    if (ImGui::Button("Edit Style"))
        showStyleEditor = true;
    ImGui::EndHorizontal();
    ImGui::Checkbox("Show Ordinals", &m_ShowOrdinals);

    if (showStyleEditor)
        ShowStyleEditor(&showStyleEditor);

    ShowNodeSelection(paneWidth);
    ShowCompilerInfo(paneWidth);
}

void Example::ContextMenu()
{
    if (ImGui::BeginPopupContextItem("SelectablePopup")) {
        // Menu options
        if (ImGui::MenuItem("Edit")) {
            // Handle Edit option
        }
        if (ImGui::MenuItem("Delete")) {
            // Handle Delete option
        }
        ImGui::EndPopup();
    }
}

void Example::ShowLeftPane(float paneWidth)
{
    auto& io = ImGui::GetIO();

    ImGui::BeginChild("Selection", ImVec2(paneWidth, 0));

    paneWidth = ImGui::GetContentRegionAvail().x;

    if (ImGui::Button("Delete Item")) {
        ImGui::OpenPopup("Confirm Delete");
    }

    if (ImGui::BeginTabBar("Tabs"))
    {
        if (ImGui::BeginTabItem("Script"))
        {
            int restoreIconWidth = GetTextureWidth(m_RestoreIcon);
            int restoreIconHeight = GetTextureWidth(m_RestoreIcon);

            // Update tree view data
            RenderTreeNode(m_scriptTreeView, m_selectedItemId, m_editingItemId);
           
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Compiler"))
        {
            ShowDebugPanel(paneWidth);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();
}

void Example::OnFrame(float deltaTime)
{
    // Pending actions
    for (auto& action : pendingActions)
    {
        action->Run();
        DoAction(action);
    }

    pendingActions.clear();

    m_graphView.OnFrame(deltaTime);

    m_validationReport = ScriptValidator::Validate(m_script);
    m_graphView.validationReport = &m_validationReport;

    VM& vm = VM::getInstance();
    Compiler& compiler = vm.getCompiler();

    // Traverse graph to see which nodes are processed, in order to display them enabled in the graph view
    if (m_validationReport.HasErrors())
        m_graphView.processedNodes.clear();
    else
        m_graphView.processedNodes = GatherProcessedNodes(*m_graphView.m_pGraph, compiler);

    auto& io = ImGui::GetIO();

    ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

    ImGui::SameLine();
    ImGuiUtils::BeginDisabled(!CanUndo());
    if (ImGui::Button("Undo"))
    {
        UndoLastAction();
    }
    ImGuiUtils::EndDisabled();
    ImGui::SameLine();
    ImGuiUtils::BeginDisabled(!CanRedo());
    if (ImGui::Button("Redo"))
    {
        RedoLastAction();
    }
    ImGuiUtils::EndDisabled();

    ImGui::SameLine();
    ShowFileControls();

    //auto& style = ImGui::GetStyle();

# if 0
    {
        for (auto x = -io.DisplaySize.y; x < io.DisplaySize.x; x += 10.0f)
        {
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, 0), ImVec2(x + io.DisplaySize.y, io.DisplaySize.y),
                IM_COL32(255, 255, 0, 255));
        }
    }
# endif

    static float leftPaneWidth = 400.0f;
    static float rightPaneWidth = 800.0f;
    Splitter(true, 4.0f, &leftPaneWidth, &rightPaneWidth, 50.0f, 50.0f);

    ShowLeftPane(leftPaneWidth - 4.0f);


    ImGui::SameLine(0.0f, 12.0f);

    m_graphView.DrawNodeEditor(m_HeaderBackground, GetTextureWidth(m_HeaderBackground), GetTextureHeight(m_HeaderBackground));

    auto editorMin = ImGui::GetItemRectMin();
    auto editorMax = ImGui::GetItemRectMax();

    if (m_ShowOrdinals)
    {
        auto drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(editorMin, editorMax);

        int ordinal = 0;
        for (const ProcessedNode& node : m_graphView.processedNodes)
        {
            auto p0 = ed::GetNodePosition(node.node->ID);
            auto p1 = p0 + ed::GetNodeSize(node.node->ID);
            p0 = ed::CanvasToScreen(p0);
            p1 = ed::CanvasToScreen(p1);


            ImGuiTextBuffer builder;
            builder.appendf("#%d", ordinal++);

            builder.append(" (");

            for (int stackFrame : node.stackFrames)
            {
                builder.appendf("%d", stackFrame);
                if (stackFrame != node.stackFrames.back())
                    builder.append(",");
            }

            builder.append(")");

            auto textSize = ImGui::CalcTextSize(builder.c_str());
            auto padding = ImVec2(2.0f, 2.0f);
            auto widgetSize = textSize + padding * 2;

            auto widgetPosition = ImVec2(p1.x, p0.y) + ImVec2(0.0f, -widgetSize.y);

            drawList->AddRectFilled(widgetPosition, widgetPosition + widgetSize, IM_COL32(100, 80, 80, 190), 3.0f, ImDrawFlags_RoundCornersAll);
            drawList->AddRect(widgetPosition, widgetPosition + widgetSize, IM_COL32(200, 160, 160, 190), 3.0f, ImDrawFlags_RoundCornersAll);
            drawList->AddText(widgetPosition + padding, IM_COL32(255, 255, 255, 255), builder.c_str());
        }

        drawList->PopClipRect();
    }


    //ImGui::ShowTestWindow();
    //ImGui::ShowMetricsWindow();
}

TreeNode Example::MakeFunctionNode(int funId, const std::string& name)
{
    TreeNode funcNode;
    funcNode.id = funId;
    funcNode.icon = m_FunctionIcon;
    funcNode.label = name;
    funcNode.onclick = [this, funId]()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
        {
            ChangeGraph(pFun);
        }
    };
    funcNode.onRename = [this, funId](std::string newName)
    {
        pendingActions.push_back(std::make_shared<RenameFunctionAction>(this, funId, newName.c_str()));
    };
    funcNode.contextMenu = [this, funId]()
    {
        if (ImGui::BeginPopupContextItem("FuncPopup"))
        {
            // Menu options
            if (ImGui::MenuItem("Add Input"))
            {
                pendingActions.push_back(std::make_shared<AddFunctionInputAction>(this, funId, m_IDGenerator.GetNextId()));
            }
            if (ImGui::MenuItem("Add Output"))
            {
                pendingActions.push_back(std::make_shared<AddFunctionOutputAction>(this, funId, m_IDGenerator.GetNextId()));
            }
            if (ImGui::MenuItem("Rename"))
            {
                m_editingItemId = funId;
            }
            if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
            {
                if (ImGui::MenuItem("Delete"))
                {
                    pendingActions.push_back(std::make_shared<DeleteFunctionAction>(this, pFun));
                }
            }
            ImGui::EndPopup();
        }
    };

    if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
    {
        funcNode.pElement = std::static_pointer_cast<IScriptElement>(pFun);
    }

    return funcNode;
}

TreeNode Example::MakeVariableNode(int varId, const std::string& name)
{
    TreeNode varNode;
    varNode.label = name;
    varNode.icon = m_VariableIcon;
    varNode.id = varId;
    varNode.onRename = [this, varId](std::string newName)
    {
        pendingActions.push_back(std::make_shared<RenameVariableAction>(this, varId, newName.c_str()));
    };
    varNode.contextMenu = [this, varId]()
    {
        if (ScriptPropertyPtr pVar = ScriptUtils::FindVariableById(m_script, varId))
        {
            if (ImGui::BeginPopupContextItem("VarPopup"))
            {
                // Menu options
                if (ImGui::MenuItem("Rename"))
                {
                    m_editingItemId = varId;
                }
                if (ImGui::MenuItem("Delete"))
                {
                    pendingActions.push_back(std::make_shared<DeleteVariableAction>(this, pVar));
                }
                ImGui::EndPopup();
            }

            ImGui::PushID(varId);
            ImGui::SameLine();
            ImGui::SetItemAllowOverlap();
            Value tmp = pVar->defaultValue;
            if (GraphViewUtils::DrawTypeInput(TypeOfValue(tmp), tmp))
            {
                pendingActions.push_back(std::make_shared<ChangeVariableValueAction>(this, varId, tmp));
            }
            ImGui::SameLine();
            ImGui::SetItemAllowOverlap();
            GraphViewUtils::DrawTypeSelection(pVar->defaultValue, [&](PinType newType)
            {
                pendingActions.push_back(std::make_shared<ChangeVariableValueAction>(this, varId, MakeValueFromType(newType)));
            });
            ImGui::PopID();
        }
    };
    if (ScriptPropertyPtr pVar = ScriptUtils::FindVariableById(m_script, varId))
    {
        varNode.pElement = std::static_pointer_cast<IScriptElement>(pVar);
    }

    return varNode;
}

TreeNode Example::MakeInputNode(int funId, int inputId, const std::string& name)
{
    TreeNode inputNode;
    inputNode.id = inputId;
    inputNode.icon = m_InputIcon;
    inputNode.label = name;
    inputNode.onRename = [this, funId, inputId](std::string newName)
    {
        pendingActions.push_back(std::make_shared<RenameFunctionInputAction>(this, funId, inputId, newName.c_str()));
    };
    inputNode.contextMenu = [this, funId, inputId]()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
        {
            if (BasicFunctionDef::Input* pInput = pFun->functionDef->FindInputByID(inputId))
            {
                if (ImGui::BeginPopupContextItem("InputPopup"))
                {
                    // Menu options
                    if (ImGui::MenuItem("Delete"))
                    {
                        pendingActions.push_back(std::make_shared<DeleteFunctionInputAction>(this, funId, inputId, pInput->name.c_str(), pInput->value));
                    }
                    if (ImGui::MenuItem("Rename"))
                    {
                        m_editingItemId = inputId;
                    }
                    ImGui::EndPopup();
                }

                Value& inputValue = pInput->value;
                ImGui::PushID(funId);
                ImGui::PushID(inputId);
                ImGui::SameLine();
                ImGui::SetItemAllowOverlap();
                Value tmp = inputValue;
                if (GraphViewUtils::DrawTypeInput(TypeOfValue(tmp), tmp))
                {
                    pendingActions.push_back(std::make_shared<ChangeFunctionInputValueAction>(this, funId, inputId, tmp));
                }
                ImGui::SameLine();
                ImGui::SetItemAllowOverlap();
                GraphViewUtils::DrawTypeSelection(inputValue, [&](PinType newType)
                {
                    pendingActions.push_back(std::make_shared<ChangeFunctionInputValueAction>(this, funId, inputId, MakeValueFromType(newType)));
                });
                ImGui::PopID();
                ImGui::PopID();
            }
        }
    };

    return inputNode;
}

TreeNode Example::MakeOutputNode(int funId, int outputId, const std::string& name)
{
    TreeNode outputNode;
    outputNode.id = outputId;
    outputNode.icon = m_OutputIcon;
    outputNode.label = name;
    outputNode.onRename = [this, funId, outputId](std::string newName)
    {
        pendingActions.push_back(std::make_shared<RenameFunctionOutputAction>(this, funId, outputId, newName.c_str()));
    };
    outputNode.contextMenu = [this, funId, outputId]()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
        {
            if (BasicFunctionDef::Input* pOutput = pFun->functionDef->FindOutputByID(outputId))
            {
                if (ImGui::BeginPopupContextItem("OutputPopup"))
                {
                    // Menu options
                    if (ImGui::MenuItem("Delete"))
                    {
                        pendingActions.push_back(std::make_shared<DeleteFunctionInputAction>(this, funId, outputId, pOutput->name.c_str(), pOutput->value));
                    }
                    if (ImGui::MenuItem("Rename"))
                    {
                        m_editingItemId = outputId;
                    }
                    ImGui::EndPopup();
                }

                Value& inputValue = pOutput->value;
                ImGui::PushID(funId);
                ImGui::PushID(outputId);
                ImGui::SameLine();
                ImGui::SetItemAllowOverlap();
                Value tmp = inputValue;
                if (GraphViewUtils::DrawTypeInput(TypeOfValue(tmp), tmp))
                {
                    pendingActions.push_back(std::make_shared<ChangeFunctionOutputValueAction>(this, funId, outputId, tmp));
                }
                ImGui::SameLine();
                ImGui::SetItemAllowOverlap();
                GraphViewUtils::DrawTypeSelection(inputValue, [&](PinType newType)
                {
                    pendingActions.push_back(std::make_shared<ChangeFunctionOutputValueAction>(this, funId, outputId, MakeValueFromType(newType)));
                });
                ImGui::PopID();
                ImGui::PopID();
            }
        }
    };

    return outputNode;
}

TreeNode* Example::FindNodeByID(int id)
{
    // TODO: Make an index of tree elements
    // Also, we probably should do the same with script elements
    std::stack<TreeNode*> pending;
    pending.push(&m_scriptTreeView);

    while (!pending.empty())
    {
        TreeNode* current = pending.top();
        pending.pop();

        if (current->id == id)
            return current;

        for (TreeNode& child : current->children)
        {
            pending.push(&child);
        }
    }

    return nullptr;
}

void Example::EraseNodeByID(int id)
{
    if (TreeNode* pNode = FindNodeByID(id))
    {
        if (TreeNode* pParentNode = FindNodeByID(pNode->parentId))
        {
            stl::erase_if(pParentNode->children, [id](const TreeNode& node) { return node.id == id; });
        }
    }
}

void Example::AddFunction(int funId)
{
    const std::string namestr = Utils::FindValidName("Func", m_scriptTreeView);

    ScriptFunctionPtr foo = std::make_shared<ScriptFunction>(funId, namestr.c_str());

    NodePtr beginFoo = BuildBeginNode(m_IDGenerator, foo);
    NodeUtils::BuildNode(beginFoo);
    foo->Graph.AddNode(beginFoo);

    m_script.functions.push_back(foo);

    // Update tree view
    const TreeNode funcNode = MakeFunctionNode(funId, namestr);
    m_scriptTreeView.AddChild(funcNode);
}

void Example::AddFunction(const ScriptFunctionPtr& pExistingFunction)
{
    m_script.functions.push_back(pExistingFunction);

    // Update tree view
    const BasicFunctionDefPtr& pFunctionDef = pExistingFunction->functionDef;
    const TreeNode funcNode = MakeFunctionNode(pExistingFunction->ID, pFunctionDef->name);
    m_scriptTreeView.AddChild(funcNode);

    if (TreeNode* funcNode = FindNodeByID(pExistingFunction->ID))
    {
        for (auto& input : pFunctionDef->inputs)
        {
            const TreeNode inputNode = MakeInputNode(pExistingFunction->ID, input.id, input.name);
            funcNode->AddChild(inputNode);
        }

        for (auto& output : pFunctionDef->outputs)
        {
            const TreeNode outputNode = MakeOutputNode(pExistingFunction->ID, output.id, output.name);
            funcNode->AddChild(outputNode);
        }

        // TODO: Variables
    }

    ScriptUtils::RefreshFunctionRefs(m_script, pExistingFunction->ID, m_IDGenerator);
}

void Example::AddVariable(int varId)
{
    const std::string namestr = Utils::FindValidName("Variable", m_scriptTreeView);

    m_script.variables.push_back(std::make_shared<ScriptProperty>(varId, namestr.c_str()));

    // Update tree view
    const TreeNode varNode = MakeVariableNode(varId, namestr);
    m_scriptTreeView.AddChild(varNode);

    ScriptUtils::RefreshVariableRefs(m_script, varId, m_IDGenerator);
}

void Example::AddVariable(const ScriptPropertyPtr& pVariable)
{
    m_script.variables.push_back(pVariable);

    // Update tree view
    const TreeNode varNode = MakeVariableNode(pVariable->ID, pVariable->Name);
    m_scriptTreeView.AddChild(varNode);

    ScriptUtils::RefreshVariableRefs(m_script, pVariable->ID, m_IDGenerator);
}

void Example::ChangeVariableValue(int id, Value& value)
{
    if (ScriptPropertyPtr pVar = ScriptUtils::FindVariableById(m_script, id))
    {
        pVar->defaultValue = value;
    }

    ScriptUtils::RefreshVariableRefs(m_script, id, m_IDGenerator);
}

void Example::RenameFunction(int funId, const char* name)
{
    ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId);
    TreeNode* pFunNode = FindNodeByID(funId);
    if (pFunNode && pFun)
    {
        pFunNode->label = name;
        pFun->functionDef->name = name;

        ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
    }
}

void Example::RenameVariable(int varId, const char* name)
{
    if (ScriptPropertyPtr pVar = ScriptUtils::FindVariableById(m_script, varId))
    {
        pVar->Name = name;
    }

    if (TreeNode* pVarNode = FindNodeByID(varId))
    {
        pVarNode->label = name;
    }

    ScriptUtils::RefreshVariableRefs(m_script, varId, m_IDGenerator);

}

void Example::AddFunctionInput(int funId, int inputId)
{
    ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId);
    TreeNode* pFunNode = FindNodeByID(funId);
    if (pFunNode && pFun)
    {
        const std::string namestr = Utils::FindValidName("Input", *pFunNode);

        pFun->functionDef->inputs.push_back({ namestr, Value(), inputId });

        // Update tree view
        const TreeNode inputNode = MakeInputNode(funId, inputId, namestr);
        pFunNode->AddChild(inputNode);

        ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
    }
}

void Example::AddFunctionInput(int funId, int inputId, const char* name, const Value& value)
{
    ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId);
    TreeNode* pFunNode = FindNodeByID(funId);
    if (pFunNode && pFun)
    {
        pFun->functionDef->inputs.push_back({ name, value, inputId });

        // Update tree view
        const TreeNode inputNode = MakeInputNode(funId, inputId, name);
        pFunNode->AddChild(inputNode);

        ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
    }
}

void Example::ChangeFunctionInputValue(int funId, int inputId, Value& value)
{
    if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
    {
        if (BasicFunctionDef::Input* pInput = pFun->functionDef->FindInputByID(inputId))
        {
            pInput->value = value;
        }
    }

    ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
}

void Example::RenameFunctionInput(int funId, int inputId, const char* name)
{
    if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
    {
        if (BasicFunctionDef::Input* pInput = pFun->functionDef->FindInputByID(inputId))
        {
            pInput->name = name;
        }
    }

    ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);

    // Update tree view
    if (TreeNode* pInputNode = FindNodeByID(inputId))
    {
        pInputNode->label = name;
    }
}

void Example::AddFunctionOutput(int funId, int outputId)
{
    ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId);
    TreeNode* pFunNode = FindNodeByID(funId);
    if (pFunNode && pFun)
    {
        const std::string namestr = Utils::FindValidName("Output", *pFunNode);

        pFun->functionDef->outputs.push_back({ namestr, Value(), outputId });

        // Update tree view
        const TreeNode outputNode = MakeOutputNode(funId, outputId, namestr);
        pFunNode->AddChild(outputNode);

        ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
    }
}

void Example::AddFunctionOutput(int funId, int outputId, const char* name, const Value& value)
{
    ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId);
    TreeNode* pFunNode = FindNodeByID(funId);
    if (pFunNode && pFun)
    {
        pFun->functionDef->outputs.push_back({ name, value, outputId });

        // Update tree view
        const TreeNode outputNode = MakeOutputNode(funId, outputId, name);
        pFunNode->AddChild(outputNode);

        ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
    }
}

void Example::ChangeFunctionOutputValue(int funId, int outputId, Value& value)
{
    if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
    {
        if (BasicFunctionDef::Input* pOutput = pFun->functionDef->FindOutputByID(outputId))
        {
            pOutput->value = value;
        }
    }

    ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
}

void Example::RenameFunctionOutput(int funId, int outputId, const char* name)
{
    if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
    {
        if (BasicFunctionDef::Input* pOutput = pFun->functionDef->FindOutputByID(outputId))
        {
            pOutput->name = name;
        }
    }

    ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);

    // Update tree view
    if (TreeNode* pOutputNode = FindNodeByID(outputId))
    {
        pOutputNode->label = name;
    }
}

void Example::RemoveFunction(int funId)
{
    stl::erase_if(m_script.functions, [funId](const ScriptFunctionPtr& func) { return func->ID == funId; });

    // Update tree view
    EraseNodeByID(funId);
 
    ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
}

void Example::RemoveVariable(int id)
{
    stl::erase_if(m_script.variables, [id](const ScriptPropertyPtr& variable) { return variable->ID == id; });

    // Update tree view
    EraseNodeByID(id);
    
    ScriptUtils::RefreshVariableRefs(m_script, id, m_IDGenerator);
}

void Example::RemoveFunctionInput(int funId, int inputId)
{
    ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId);

    if (pFun)
    {
        stl::erase_if(pFun->functionDef->inputs, [inputId](const BasicFunctionDef::Input& input){
            return input.id == inputId;
        });
    }

    // Update tree view
    EraseNodeByID(inputId);

    ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
}

void Example::RemoveFunctionOutput(int funId, int outputId)
{
    ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId);

    if (pFun)
    {
        stl::erase_if(pFun->functionDef->outputs, [outputId](const BasicFunctionDef::Input& input) {
            return input.id == outputId;
        });
    }

    // Update tree view
    EraseNodeByID(outputId);

    ScriptUtils::RefreshFunctionRefs(m_script, funId, m_IDGenerator);
}

void Example::DoAction(IActionPtr action)
{
    // Remove top of the stack
    actionStack.resize(actionStack.size() - undoDepth);
    undoDepth = 0;

    // Push new action
    actionStack.push_back(action);
}

void Example::UndoLastAction()
{
    IActionPtr action = actionStack[actionStack.size() - undoDepth - 1];
    action->Revert();
    undoDepth++;
}

void Example::RedoLastAction()
{
    IActionPtr action = actionStack[actionStack.size() - undoDepth];
    action->Run();
    undoDepth--;
}

bool Example::CanUndo() const
{
    const int undoActionIdx = actionStack.size() - undoDepth - 1;
    return undoActionIdx >= 0;
}

bool Example::CanRedo() const
{
    const int redoActionIdx = actionStack.size() - undoDepth;
    return redoActionIdx < actionStack.size();
}

void Example::InitializeScriptTree()
{
    m_scriptTreeView = TreeNode{};
    m_scriptTreeView.label = "Script";
    m_scriptTreeView.isOpen = true;
    m_scriptTreeView.icon = m_ScriptIcon;
    m_scriptTreeView.id = m_script.ID;
    m_scriptTreeView.contextMenu = [this]()
    {
        if (ImGui::BeginPopupContextItem("SelectablePopup"))
        {
            if (ImGui::MenuItem("Add Function"))
                pendingActions.push_back(std::make_shared<AddFunctionAction>(this, m_IDGenerator.GetNextId()));
            if (ImGui::MenuItem("Add Variable"))
                pendingActions.push_back(std::make_shared<AddVariableAction>(this, m_IDGenerator.GetNextId()));
            ImGui::EndPopup();
        }
    };
}

void Example::RebuildScriptTree()
{
    InitializeScriptTree();

    if (m_script.main)
    {
        TreeNode mainNode;
        mainNode.id = m_script.main->ID;
        mainNode.label = m_script.main->functionDef->name;
        mainNode.icon = m_FunctionIcon;
        mainNode.onclick = [this]() { ChangeGraph(m_script.main); };
        m_scriptTreeView.AddChild(mainNode);
    }

    for (const ScriptFunctionPtr& function : m_script.functions)
    {
        m_scriptTreeView.AddChild(MakeFunctionNode(function->ID, function->functionDef->name));
        TreeNode* functionNode = FindNodeByID(function->ID);
        if (!functionNode)
            continue;
        for (const BasicFunctionDef::Input& input : function->functionDef->inputs)
            functionNode->AddChild(MakeInputNode(function->ID, input.id, input.name));
        for (const BasicFunctionDef::Input& output : function->functionDef->outputs)
            functionNode->AddChild(MakeOutputNode(function->ID, output.id, output.name));
    }

    for (const ScriptPropertyPtr& variable : m_script.variables)
        m_scriptTreeView.AddChild(MakeVariableNode(variable->ID, variable->Name));
}

void Example::SaveScript(const std::string& path)
{
    const SerializationResult result = ScriptSerializer::Save(m_script, path);
    m_fileStatusIsError = !result;
    if (!result)
    {
        m_fileStatus = "Save failed: " + result.error;
        return;
    }

    m_currentScriptPath = path;
    m_fileStatus = "Saved " + std::filesystem::path(path).filename().string();
    SetTitle(("VisualLox - " + std::filesystem::path(path).filename().string()).c_str());
}

void Example::LoadScript(const std::string& path)
{
    Script loadedScript;
    IDGenerator loadedIds;
    const SerializationResult result = ScriptSerializer::Load(path, m_NodeRegistry, loadedScript, loadedIds);
    m_fileStatusIsError = !result;
    if (!result)
    {
        m_fileStatus = "Open failed: " + result.error;
        return;
    }

    m_graphView.Destroy();
    m_script = std::move(loadedScript);
    m_IDGenerator = loadedIds;
    pendingActions.clear();
    actionStack.clear();
    undoDepth = 0;
    m_constFoldingValues.clear();
    m_constFoldingIDs.clear();
    m_selectedItemId = m_script.main ? m_script.main->ID.id : 0;
    m_editingItemId = 0;
    RebuildScriptTree();
    m_graphView.SetGraph(&m_script, m_script.main, &m_script.main->Graph);

    m_currentScriptPath = path;
    m_fileStatus = "Opened " + std::filesystem::path(path).filename().string();
    SetTitle(("VisualLox - " + std::filesystem::path(path).filename().string()).c_str());
}

void Example::ShowFileControls()
{
    if (ImGui::Button("Open"))
    {
        if (const std::optional<std::string> path = SelectVloxFile(false, m_currentScriptPath))
            LoadScript(*path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        if (!m_currentScriptPath.empty())
            SaveScript(m_currentScriptPath);
        else if (const std::optional<std::string> path = SelectVloxFile(true, "Untitled.vlox"))
            SaveScript(*path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As"))
    {
        const std::string suggested = m_currentScriptPath.empty() ? "Untitled.vlox" : m_currentScriptPath;
        if (const std::optional<std::string> path = SelectVloxFile(true, suggested))
            SaveScript(*path);
    }

    if (!m_fileStatus.empty())
    {
        ImGui::SameLine();
        if (m_fileStatusIsError)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_fileStatus.c_str());
        else
            ImGui::TextDisabled("%s", m_fileStatus.c_str());
    }
}

}
