#define IMGUI_DEFINE_MATH_OPERATORS

#include <application.h>
#include "utilities/builders.h"
#include "utilities/widgets.h"

#include "graphs/link.h"
#include "graphs/node.h"
#include "graphs/graphView.h"
#include "graphs/graphCompiler.h"

#include "script/script.h"

#include "native/nodes/begin.h"
#include "native/nodes/branch.h"
#include "native/nodes/print.h"
#include "native/nodes/for-in.h"
#include "native/nodes/math.h"
#include "native/nodes/list.h"

#include "graphs/nodeRegistry.h"

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


namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

using namespace ax;

using ax::Widgets::IconType;

static ed::EditorContext* m_Editor = nullptr;

//extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vkey);
//extern "C" bool Debug_KeyPress(int vkey)
//{
//    static std::map<int, bool> state;
//    auto lastState = state[vkey];
//    state[vkey] = (GetAsyncKeyState(vkey) & 0x8000) != 0;
//    if (state[vkey] && !lastState)
//        return true;
//    else
//        return false;
//}

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

namespace Utils
{
    void DrawEachLine(const std::string& text)
    {
        std::stringstream stream(text);
        std::string segment;

        while (std::getline(stream, segment, '\n'))
        {
            ImGui::Text(segment.c_str());
        }
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

struct Example:
    public Application
{
    using Application::Application;

    void OnStart() override
    {
        m_graphView.setIDGenerator(m_IDGenerator);
        m_graphView.setNodeRegistry(m_NodeRegistry);
        m_graphView.SetScript(&m_script);
        m_graphView.SetGraph(&m_script.main.Graph);

        m_HeaderBackground = LoadTexture("data/BlueprintBackground.png");
        m_SaveIcon         = LoadTexture("data/ic_save_white_24dp.png");
        m_RestoreIcon      = LoadTexture("data/ic_restore_white_24dp.png");

        VM& vm = VM::getInstance();
        vm.setExternalMarkingFunc([&]()
        {
            for (NodePtr& node : m_graphView.m_pGraph->GetNodes())
            {
                for (Value& value : node->InputValues)
                {
                    vm.markValue(value);
                }
            }

            for (NativeFunctionDefPtr& def : m_NodeRegistry.nativeDefinitions)
            {
                for (auto& input : def->inputs)
                {
                    vm.markValue(input.value);
                }

                for (auto& input : def->outputs)
                {
                    vm.markValue(input.value);
                }
            }

            for (Value& value : m_constFoldingValues)
            {
                vm.markValue(value);
            }

            for (ScriptClass& scriptClass : m_script.classes)
            {
                for (ScriptFunction& scriptFunction : scriptClass.methods)
                {
                    for (auto& input : scriptFunction.Inputs)
                    {
                        vm.markValue(input.value);
                    }

                    for (auto& output : scriptFunction.Outputs)
                    {
                        vm.markValue(output.value);
                    }
                }

                for (ScriptProperty& scriptProperty : scriptClass.properties)
                {
                    vm.markValue(scriptProperty.defaultValue);
                }
            }

            for (ScriptFunction& scriptFunction : m_script.functions)
            {
                for (auto& input : scriptFunction.Inputs)
                {
                    vm.markValue(input.value);
                }

                for (auto& output : scriptFunction.Outputs)
                {
                    vm.markValue(output.value);
                }
            }

            for (ScriptProperty& scriptProperty : m_script.variables)
            {
                vm.markValue(scriptProperty.defaultValue);
            }
        });
        
        m_NodeRegistry.RegisterCompiledNode("Flow::Branch", &BuildBranchNode);
        m_NodeRegistry.RegisterCompiledNode("Flow::For In", &BuildForInNode);
        m_NodeRegistry.RegisterCompiledNode("Debug::Print", &BuildPrintNode);
        m_NodeRegistry.RegisterCompiledNode("String::Append", &CreateAppendNode);
        m_NodeRegistry.RegisterCompiledNode("String::CreateString", &CreateString);
        m_NodeRegistry.RegisterCompiledNode("Math::Add", &CreateAddNode);
        m_NodeRegistry.RegisterCompiledNode("Math::Subtract", &CreateSubtractNode);
        m_NodeRegistry.RegisterCompiledNode("Math::Multiply", &CreateMultiplyNode);
        m_NodeRegistry.RegisterCompiledNode("Math::Divide", &CreateDivideNode);
        m_NodeRegistry.RegisterCompiledNode("Math::Greater Than", &CreateGreaterNode);
        m_NodeRegistry.RegisterCompiledNode("Math::Less Than", &CreateLessNode);
        m_NodeRegistry.RegisterCompiledNode("Math::Modulo", &CreateModuloNode);
        m_NodeRegistry.RegisterCompiledNode("List::Get By Index", &BuildListGetByIndexNode);
        m_NodeRegistry.RegisterCompiledNode("List::Set By Index", &BuildListSetByIndexNode);

        m_NodeRegistry.RegisterDefinitions();

        // Add test script variables
        m_script.variables.push_back({ "Variables::MyVar", Value(takeString("Hello World", 11)) });
        m_script.variables.push_back({ "Variables::Amount", Value(11.0) });

        //auto& io = ImGui::GetIO();
    }

    void OnStop() override
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
    }

    void ShowStyleEditor(bool* show = nullptr)
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

    InterpretResult CompileConstFolding(VM& vm, const NodePtr& constNode)
    {
        std::cout << std::endl << "Compiling node: " << constNode->Name << std::endl;
        // Compile code
        Compiler& compiler = vm.getCompiler();
        compiler.beginCompile();

        const Token resultToken(TokenType::VAR, "__res", 5, 0);

        compiler.beginScope();

        GraphCompiler graphCompiler(compiler);

        auto callback = [&](const NodePtr& node, const Graph& graph, CompilationStage stage, int portIdx)
        {
            node->Compile(graphCompiler.context, graph, stage, portIdx);
        };
        graphCompiler.CompileSingle(*m_graphView.m_pGraph, constNode, -1, 0, callback);

        // Store result in a global
        const uint32_t constant = compiler.identifierConstant(resultToken);
        compiler.emitOpWithValue(OpCode::OP_DEFINE_GLOBAL, OpCode::OP_DEFINE_GLOBAL_LONG, constant);

        compiler.endScope();

        compiler.emitVariable(resultToken, false);

        // We return that value directly
        compiler.emitByte(OpByte(OpCode::OP_RETURN));
        ObjFunction* function = compiler.current->function;

        if (function != nullptr)
        {
            vm.push(Value(function));
            ObjClosure* closure = newClosure(function);
            vm.pop();
            vm.push(Value(closure));
            vm.callValue(Value(closure), 0);

            return vm.run(0);
        }

        return InterpretResult::INTERPRET_COMPILE_ERROR;
    }

    void ShowNodeSelection(float paneWidth)
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

    void ShowCompilerInfo(float paneWidth)
    {
        static std::string result = "<output>";
        static std::string runResult = "";

        ImGui::Checkbox("Const Folding Enabled", &m_isConstFoldingEnabled);


        VM& vm = VM::getInstance();



        std::cout << std::endl;



        // Register natives if needed
        // TODO: Move somewhere else
        static bool isRegistered = false;
        if (!isRegistered)
        {
            m_NodeRegistry.RegisterNatives(vm);
            isRegistered = true;
        }

        Utils::CaptureStdout captureCompilation;

        const InterpretResult vmResult = vm.interpret("var a = 2; print a;");
        vm.resetStack();

        // Compile code
        std::cout << std::endl << "Compiling graph: " << std::endl;
        Compiler& compiler = vm.getCompiler();
        ObjFunction* function = nullptr;

        // Compile everything here!
        NodePtr begin = m_graphView.m_pGraph->FindNodeIf([](const NodePtr& node) { return node->Category == NodeCategory::Begin; });
        if (begin)
        {
            GraphCompiler graphCompiler(compiler);

            std::vector<NodePtr> processedNodes;
            // First pass to gather processed nodes and cost folding
            graphCompiler.CompileGraph(*m_graphView.m_pGraph, begin, 0, [&](const NodePtr& node, const Graph& graph, CompilationStage stage, int portIdx)
            {
                if (std::find(processedNodes.begin(), processedNodes.end(), node) == processedNodes.end())
                {
                    processedNodes.push_back(node);
                }
            });

            // Test: const folding
            m_constFoldingValues.clear();
            m_constFoldingIDs.clear();

            if (m_isConstFoldingEnabled)
            {
                for (const NodePtr& node : processedNodes)
                {
                    if (GraphUtils::IsNodeConstFoldable(*m_graphView.m_pGraph, node))
                    {
                        if (CompileConstFolding(vm, node) == InterpretResult::INTERPRET_OK)
                        {
                            m_constFoldingValues.push_back(vm.peek(-1));
                            m_constFoldingIDs.push_back(node->ID);
                        }
                    }
                }
            }

            compiler.beginCompile();

            // Compile script variables (globals)
            for (const ScriptProperty& scriptProperty : m_script.variables)
            {
                compiler.emitConstant(scriptProperty.defaultValue);
                const Token outputToken(TokenType::VAR, scriptProperty.Name.c_str(), scriptProperty.Name.length(), 0);
                const uint32_t constant = compiler.identifierConstant(outputToken);
                compiler.defineVariable(constant);
            }

            compiler.beginScope();

            graphCompiler.context.constFoldingValues = m_constFoldingValues;
            graphCompiler.context.constFoldingIDs = m_constFoldingIDs;

            graphCompiler.CompileGraph(*m_graphView.m_pGraph, begin, 0, [&](const NodePtr& node, const Graph& graph, CompilationStage stage, int portIdx)
            {
                if (stage == CompilationStage::ConstFoldedInputs)
                {
                    compiler.emitConstant(m_constFoldingValues[portIdx]);
                    const int outputIdx = GraphUtils::IsNodeImplicit(node) ? 0 : 1;
                    GraphCompiler::CompileOutput(graphCompiler.context, graph, node->Outputs[outputIdx]);
                }
                else
                {
                    node->Compile(graphCompiler.context, graph, stage, portIdx);
                }
            });

            // TODO: Do in a different way!
            m_graphView.processedNodes = processedNodes;

            compiler.endScope();
            function = compiler.endCompiler();

            // Print debug code
            disassembleChunk(compiler.compilerData.function->chunk, function->name != nullptr ? function->name->chars.c_str() : "<script>");
        }

        result = captureCompilation.Restore();

        if (ImGui::Button("Run"))
        {
            if (function != nullptr)
            {
                vm.push(Value(function));
                ObjClosure* closure = newClosure(function);
                vm.pop();
                vm.push(Value(closure));
                vm.callValue(Value(closure), 0);

                Utils::CaptureStdout captureExecution;

                const InterpretResult vmResult = vm.run(0);

                if (vmResult == InterpretResult::INTERPRET_OK)
                    vm.pop();

                if (vmResult == InterpretResult::INTERPRET_COMPILE_ERROR)
                    std::cout << "Compilation Error";
                else if (vmResult == InterpretResult::INTERPRET_RUNTIME_ERROR)
                    std::cout << "Runtime Error";

                runResult = "Execution output:\n" + captureExecution.Restore();
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
        Utils::DrawEachLine(runResult);


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

    void ShowDebugPanel(float paneWidth)
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

    void ContextMenu()
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

    void ShowLeftPane(float paneWidth)
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

                if (ImGui::TreeNodeEx("Script", ImGuiTreeNodeFlags_DefaultOpen)) {

                    for (ScriptClass& scriptClass : m_script.classes)
                    {
                        if (ImGui::TreeNode(scriptClass.Name.c_str()))
                        {
                            for (ScriptFunction& scriptFunction : scriptClass.methods)
                            {
                                if (ImGui::TreeNode(scriptFunction.Name.c_str()))
                                {
                                    ImGui::TreePop();
                                }
                            }

                            for (ScriptProperty& scriptProperty : scriptClass.properties)
                            {
                                ImGui::Text(scriptProperty.Name.c_str());
                            }

                            ImGui::TreePop();
                        }
                    }

                    for (ScriptFunction& scriptFunction : m_script.functions)
                    {
                        if (ImGui::TreeNode(scriptFunction.Name.c_str()))
                        {
                            ImGui::TreePop();
                        }
                    }

                    for (ScriptProperty& scriptProperty : m_script.variables)
                    {
                        ImGui::PushID(scriptProperty.Name.c_str());
                        ImGui::Text(scriptProperty.Name.c_str());
                        ImGui::SameLine();
                        GraphViewUtils::DrawTypeInput(PinType::Any, scriptProperty.defaultValue);
                        ImGui::PopID();
                    }

                    ImGui::Text(m_script.main.Name.c_str());

                    ImGui::TreePop();
                }

                ImGui::Text("This is the content of Tab 3.");
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

    void OnFrame(float deltaTime) override
    {
        m_graphView.OnFrame(deltaTime);

        auto& io = ImGui::GetIO();

        ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

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

        static float leftPaneWidth  = 400.0f;
        static float rightPaneWidth = 800.0f;
        Splitter(true, 4.0f, &leftPaneWidth, &rightPaneWidth, 50.0f, 50.0f);

        ShowLeftPane(leftPaneWidth - 4.0f);
        

        ImGui::SameLine(0.0f, 12.0f);

        m_graphView.DrawNodeEditor(m_HeaderBackground, GetTextureWidth(m_HeaderBackground), GetTextureHeight(m_HeaderBackground));

        auto editorMin = ImGui::GetItemRectMin();
        auto editorMax = ImGui::GetItemRectMax();

        if (m_ShowOrdinals)
        {
            int nodeCount = ed::GetNodeCount();
            std::vector<ed::NodeId> orderedNodeIds;
            orderedNodeIds.resize(static_cast<size_t>(nodeCount));
            ed::GetOrderedNodeIds(orderedNodeIds.data(), nodeCount);


            auto drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(editorMin, editorMax);

            int ordinal = 0;
            for (auto& nodeId : orderedNodeIds)
            {
                auto p0 = ed::GetNodePosition(nodeId);
                auto p1 = p0 + ed::GetNodeSize(nodeId);
                p0 = ed::CanvasToScreen(p0);
                p1 = ed::CanvasToScreen(p1);


                ImGuiTextBuffer builder;
                builder.appendf("#%d", ordinal++);

                auto textSize   = ImGui::CalcTextSize(builder.c_str());
                auto padding    = ImVec2(2.0f, 2.0f);
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

    Script               m_script;
    GraphView            m_graphView;

    ImTextureID          m_HeaderBackground = nullptr;
    ImTextureID          m_SaveIcon = nullptr;
    ImTextureID          m_RestoreIcon = nullptr;
    bool                 m_ShowOrdinals = false;
    
    IDGenerator          m_IDGenerator;
    NodeRegistry         m_NodeRegistry;

    // TODO: Move somewhere else!
    bool m_isConstFoldingEnabled = true;
    std::vector<Value>   m_constFoldingValues;
    std::vector<ed::NodeId>   m_constFoldingIDs;
};

int Main(int argc, char** argv)
{
    Example exampe("Blueprints", argc, argv);

    if (exampe.Create())
        return exampe.Run();

    return 0;
}