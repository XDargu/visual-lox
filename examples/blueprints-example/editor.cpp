#include "editor.h"

#include "native/nodes/begin.h"
#include "IconsFontAwesome6.h"


#include "utilities/utils.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stack>

#include <misc/imgui_stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

namespace Editor
{

namespace
{
const ImVec4 kAccent = ImVec4(0.25f, 0.55f, 0.95f, 1.0f);
const ImVec4 kSuccess = ImVec4(0.24f, 0.72f, 0.47f, 1.0f);
const ImVec4 kWarning = ImVec4(0.96f, 0.67f, 0.24f, 1.0f);
const ImVec4 kError = ImVec4(0.94f, 0.32f, 0.34f, 1.0f);
const ImVec4 kMuted = ImVec4(0.58f, 0.62f, 0.70f, 1.0f);

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

void Tooltip(const char* text)
{
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(text);
        ImGui::EndTooltip();
    }
}

void PanelHeading(ImFont* font, const char* icon, const char* title)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (font)
        ImGui::PushFont(font);
    ImGui::TextUnformatted(title);
    if (font)
        ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::Separator();
}

Value CloneInspectorValue(const Value& source)
{
    if (!isList(source))
        return source;

    ObjList* clone = newList();
    for (const Value& item : asList(source)->items)
        clone->append(CloneInspectorValue(item));
    return Value(clone);
}

const char* InspectorTypeName(PinType type)
{
    switch (type)
    {
    case PinType::Bool: return "Bool";
    case PinType::Float: return "Number";
    case PinType::String: return "String";
    case PinType::List: return "List";
    case PinType::Function: return "Function";
    case PinType::Range: return "Range";
    case PinType::Object: return "Object";
    case PinType::Any: return "Any";
    case PinType::Flow: return "Flow";
    default: return "Unknown";
    }
}

bool DrawInspectorValueEditor(const char* id, Value& value, bool allowTypeChange = true,
                              int depth = 0)
{
    bool changed = false;
    ImGui::PushID(id);

    if (allowTypeChange)
    {
        int currentType = 6;
        switch (TypeOfValue(value))
        {
        case PinType::Bool: currentType = 0; break;
        case PinType::Float: currentType = 1; break;
        case PinType::String: currentType = 2; break;
        case PinType::List: currentType = 3; break;
        case PinType::Function: currentType = 4; break;
        case PinType::Range: currentType = 5; break;
        default: break;
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##type", &currentType,
                         "Bool\0Number\0String\0List\0Function\0Range\0Any\0"))
        {
            static const PinType types[] = {
                PinType::Bool, PinType::Float, PinType::String, PinType::List,
                PinType::Function, PinType::Range, PinType::Any
            };
            value = MakeValueFromType(types[currentType]);
            changed = true;
        }
    }
    const PinType type = TypeOfValue(value);
    if (type == PinType::Bool)
    {
        bool boolean = asBoolean(value);
        if (ImGui::Checkbox("Value", &boolean))
        {
            value = Value(boolean);
            changed = true;
        }
    }
    else if (type == PinType::Float)
    {
        double number = asNumber(value);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputDouble("##value", &number, 0.0, 0.0, "%.15g"))
        {
            value = Value(number);
            changed = true;
        }
    }
    else if (type == PinType::String)
    {
        std::string text = asString(value)->chars;
        const float height = depth == 0 ? 78.0f : 54.0f;
        if (ImGui::InputTextMultiline("##value", &text, ImVec2(-1.0f, height)))
        {
            value = Value(copyString(text.c_str(), static_cast<int>(text.size())));
            changed = true;
        }
    }
    else if (type == PinType::Range)
    {
        ObjRange* range = asRange(value);
        double minimum = range->min;
        double maximum = range->max;
        ImGui::PushID("minimum");
        ImGui::SetNextItemWidth(-1.0f);
        const bool minimumChanged =
            ImGui::InputDouble("##value", &minimum, 0.0, 0.0, "From %.15g");
        ImGui::PopID();
        ImGui::PushID("maximum");
        ImGui::SetNextItemWidth(-1.0f);
        const bool maximumChanged =
            ImGui::InputDouble("##value", &maximum, 0.0, 0.0, "To %.15g");
        ImGui::PopID();
        if (minimumChanged || maximumChanged)
        {
            value = Value(newRange(minimum, maximum));
            changed = true;
        }
    }
    else if (type == PinType::List)
    {
        ObjList* list = asList(value);
        ImGui::TextDisabled("%zu item%s", list->items.size(),
                            list->items.size() == 1 ? "" : "s");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_PLUS " Add item"))
        {
            list->append(Value());
            changed = true;
        }

        if (depth >= 4)
        {
            ImGui::TextDisabled("Nested list depth limit reached.");
        }
        else
        {
            for (int i = 0; i < static_cast<int>(list->items.size()); ++i)
            {
                ImGui::PushID(i);
                ImGui::Separator();
                ImGui::Text("Item %d", i + 1);
                const float removeWidth =
                    ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x +
                    ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::SameLine(ImMax(ImGui::GetCursorPosX(),
                                      ImGui::GetWindowContentRegionMax().x - removeWidth));
                if (ImGui::SmallButton(ICON_FA_TRASH_CAN))
                {
                    list->deleteValue(i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                Value item = CloneInspectorValue(list->items[i]);
                if (DrawInspectorValueEditor("item", item, true, depth + 1))
                {
                    list->setValue(i, item);
                    changed = true;
                }
                ImGui::PopID();
            }
        }
    }
    else if (type == PinType::Function || type == PinType::Object)
    {
        ImGui::TextDisabled("Runtime reference (not editable as a literal)");
    }
    else
    {
        ImGui::TextDisabled("No default value");
    }

    ImGui::PopID();
    return changed;
}

void DrawVerticalSplitter(const char* id, float& size, float minSize, float maxSize,
                          float height, bool reverse = false)
{
    ImGui::InvisibleButton(id, ImVec2(5.0f, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (active)
    {
        const float delta = ImGui::GetIO().MouseDelta.x * (reverse ? -1.0f : 1.0f);
        size = ImClamp(size + delta, minSize, maxSize);
    }
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    const ImU32 color = ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive
                                                  : hovered ? ImGuiCol_SeparatorHovered
                                                            : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), color);
}

void DrawHorizontalSplitter(const char* id, float& bottomSize, float minSize,
                            float maxSize, float width)
{
    ImGui::InvisibleButton(id, ImVec2(width, 5.0f));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (active)
        bottomSize = ImClamp(bottomSize - ImGui::GetIO().MouseDelta.y, minSize, maxSize);
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    const ImU32 color = ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive
                                                  : hovered ? ImGuiCol_SeparatorHovered
                                                            : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), color);
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
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                            disabled ? ImGui::GetStyle().Alpha * 0.45f
                                     : ImGui::GetStyle().Alpha);
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
    LoadLayoutSettings();
    m_graphView.setIDGenerator(m_IDGenerator);
    m_graphView.Init(LargeNodeFont());
    m_graphView.setNodeRegistry(m_NodeRegistry);

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

    m_operations = std::make_unique<DocumentOperations>(m_script, m_IDGenerator, m_NodeRegistry);
    m_graphView.setDocumentOperations(*m_operations);

    RebuildScriptTree();
    ApplyEditorTheme();
    SetTitle("Visual Lox - Untitled");
}

void Example::OnStop()
{
    SaveLayoutSettings();
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

ImGuiWindowFlags Example::GetWindowFlags() const
{
    return Application::GetWindowFlags() | ImGuiWindowFlags_MenuBar;
}

void Example::ApplyEditorTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.89f, 0.91f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled] = kMuted;
    colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.062f, 0.078f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.072f, 0.081f, 0.102f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.084f, 0.106f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.16f, 0.18f, 0.23f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.18f, 0.23f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.22f, 0.29f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.065f, 0.073f, 0.092f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.055f, 0.062f, 0.078f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.21f, 0.24f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.29f, 0.34f, 0.43f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.36f, 0.43f, 0.55f, 1.00f);
    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.22f, 0.29f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.32f, 0.52f, 0.72f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.43f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.50f, 0.84f, 0.90f);
    colors[ImGuiCol_Separator] = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.25f, 0.50f, 0.84f, 0.80f);
    colors[ImGuiCol_SeparatorActive] = kAccent;
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.25f, 0.50f, 0.84f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.25f, 0.50f, 0.84f, 0.65f);
    colors[ImGuiCol_ResizeGripActive] = kAccent;
    colors[ImGuiCol_Tab] = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.35f, 0.58f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.14f, 0.25f, 0.41f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 0.55f, 0.95f, 0.35f);

    ed::Style& nodeStyle = ed::GetStyle();
    nodeStyle.NodeRounding = 6.0f;
    nodeStyle.NodeBorderWidth = 1.0f;
    nodeStyle.HoveredNodeBorderWidth = 2.0f;
    nodeStyle.SelectedNodeBorderWidth = 2.0f;
    nodeStyle.LinkStrength = 90.0f;
    nodeStyle.ScrollDuration = 0.22f;
    nodeStyle.Colors[ed::StyleColor_Bg] = ImColor(24, 27, 34, 255);
    nodeStyle.Colors[ed::StyleColor_Grid] = ImColor(110, 122, 145, 20);
    nodeStyle.Colors[ed::StyleColor_NodeBg] = ImColor(31, 35, 44, 248);
    nodeStyle.Colors[ed::StyleColor_NodeBorder] = ImColor(85, 95, 115, 180);
    nodeStyle.Colors[ed::StyleColor_HovNodeBorder] = ImColor(93, 159, 245, 255);
    nodeStyle.Colors[ed::StyleColor_SelNodeBorder] = ImColor(107, 174, 255, 255);
    nodeStyle.Colors[ed::StyleColor_NodeSelRect] = ImColor(64, 140, 242, 40);
    nodeStyle.Colors[ed::StyleColor_NodeSelRectBorder] = ImColor(83, 155, 250, 160);
    nodeStyle.Colors[ed::StyleColor_HovLinkBorder] = ImColor(112, 180, 255, 255);
    nodeStyle.Colors[ed::StyleColor_SelLinkBorder] = ImColor(112, 180, 255, 255);
}

void Example::LoadLayoutSettings()
{
    std::ifstream file("VisualLoxLayout.ini");
    std::string line;
    while (std::getline(file, line))
    {
        const size_t separator = line.find('=');
        if (separator == std::string::npos)
            continue;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        try
        {
            if (key == "leftPaneWidth") m_leftPaneWidth = std::stof(value);
            else if (key == "rightPaneWidth") m_rightPaneWidth = std::stof(value);
            else if (key == "bottomPaneHeight") m_bottomPaneHeight = std::stof(value);
            else if (key == "showScriptExplorer") m_showScriptExplorer = std::stoi(value) != 0;
            else if (key == "showInspector") m_showInspector = std::stoi(value) != 0;
            else if (key == "showBottomPanel") m_showBottomPanel = std::stoi(value) != 0;
            else if (key == "showDeveloperTools") m_showDeveloperTools = std::stoi(value) != 0;
        }
        catch (...)
        {
            // Ignore malformed user layout values and retain safe defaults.
        }
    }

    m_leftPaneWidth = ImClamp(m_leftPaneWidth, 220.0f, 480.0f);
    m_rightPaneWidth = ImClamp(m_rightPaneWidth, 240.0f, 480.0f);
    m_bottomPaneHeight = ImClamp(m_bottomPaneHeight, 160.0f, 440.0f);
}

void Example::SaveLayoutSettings() const
{
    std::ofstream file("VisualLoxLayout.ini", std::ios::trunc);
    if (!file)
        return;
    file << "leftPaneWidth=" << m_leftPaneWidth << '\n';
    file << "rightPaneWidth=" << m_rightPaneWidth << '\n';
    file << "bottomPaneHeight=" << m_bottomPaneHeight << '\n';
    file << "showScriptExplorer=" << (m_showScriptExplorer ? 1 : 0) << '\n';
    file << "showInspector=" << (m_showInspector ? 1 : 0) << '\n';
    file << "showBottomPanel=" << (m_showBottomPanel ? 1 : 0) << '\n';
    file << "showDeveloperTools=" << (m_showDeveloperTools ? 1 : 0) << '\n';
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
    (void)paneWidth;
    VM& vm = VM::getInstance();

    if (ImGui::CollapsingHeader("Compiler settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Enable constant folding", &m_isConstFoldingEnabled);
        ImGui::Checkbox("Validate as you edit", &m_isRealTimeCompilationEnabled);
        ImGui::Checkbox("Show graph ordinals", &m_ShowOrdinals);
    }

    if (ImGui::CollapsingHeader("Compiled output"))
        ImGui::TextUnformatted(m_compileOutput.c_str());

    if (ImGui::CollapsingHeader("String table"))
    {
        const Table& table = vm.stringTable();
        for (size_t i = 0; i < table.getEntriesSize(); ++i)
            if (const Entry* entry = table.getEntryByIndex(i); entry && entry->key)
                ImGui::BulletText("%s", entry->key->chars.c_str());
    }

    if (ImGui::CollapsingHeader("Globals"))
    {
        const Table& table = vm.globalTable();
        for (size_t i = 0; i < table.getEntriesSize(); ++i)
            if (const Entry* entry = table.getEntryByIndex(i); entry && entry->key)
                ImGui::BulletText("%s  %s", entry->key->chars.c_str(),
                                  valueAsStr(entry->value).c_str());
    }

    if (ImGui::CollapsingHeader("Folded nodes"))
    {
        for (size_t i = 0; i < m_constFoldingIDs.size(); ++i)
            ImGui::BulletText("%p  %s", m_constFoldingIDs[i].AsPointer(),
                              valueAsStr(m_constFoldingValues[i]).c_str());
    }
}

void Example::ShowDebugPanel(float paneWidth)
{
    (void)paneWidth;
    ShowDeveloperPanel();
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
    (void)paneWidth;
    ShowScriptExplorer();
}

void Example::ShowScriptExplorer()
{
    PanelHeading(HeaderFont(), ICON_FA_FILE_CODE, "Script Explorer");

    ScriptClassPtr selectedClass;
    ScriptFunctionPtr selectedFunction;
    for (TreeNode* item = FindNodeByID(m_selectedItemId); item;
         item = item->parentId >= 0 ? FindNodeByID(item->parentId) : nullptr)
    {
        if (!selectedClass)
            selectedClass = std::dynamic_pointer_cast<ScriptClass>(item->pElement);
        if (!selectedFunction)
            selectedFunction = std::dynamic_pointer_cast<ScriptFunction>(item->pElement);
    }
    if (!selectedClass && selectedFunction)
        selectedClass = ScriptUtils::FindOwningClass(m_script, selectedFunction->ID.id);

    const float addButtonWidth = ImGui::CalcTextSize(ICON_FA_PLUS).x +
                                 ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImMax(80.0f, ImGui::GetContentRegionAvail().x -
                                         addButtonWidth - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##scriptFilter", ICON_FA_MAGNIFYING_GLASS " Filter script...",
                             &m_scriptFilter);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS "##addScriptItem"))
        ImGui::OpenPopup("Add script item");
    Tooltip(selectedClass ? "Add a member to the selected class"
                          : selectedFunction ? "Add a function port"
                                             : "Add a script item");

    if (ImGui::BeginPopup("Add script item"))
    {
        if (selectedClass)
        {
            ImGui::TextDisabled("%s members", selectedClass->Name.c_str());
            if (ImGui::MenuItem(ICON_FA_DATABASE "  Property"))
            {
                const int classId = selectedClass->ID.id;
                const int propertyId = m_IDGenerator.GetNextId();
                pendingActions.push_back(std::make_shared<DeferredAction>(
                    [this, classId, propertyId]()
                    {
                        const OperationResult result =
                            m_operations->AddClassProperty(classId, propertyId, "Property");
                        m_fileStatusIsError = !result;
                        m_fileStatus = result ? "Class property added" : result.error;
                        if (result) RebuildScriptTree();
                    }));
            }
            if (ImGui::MenuItem(ICON_FA_DIAGRAM_PROJECT "  Method"))
            {
                const int classId = selectedClass->ID.id;
                const int methodId = m_IDGenerator.GetNextId();
                pendingActions.push_back(std::make_shared<DeferredAction>(
                    [this, classId, methodId]()
                    {
                        const OperationResult result =
                            m_operations->AddClassMethod(classId, methodId, "Method");
                        m_fileStatusIsError = !result;
                        m_fileStatus = result ? "Class method added" : result.error;
                        if (result) RebuildScriptTree();
                    }));
            }
            if (!selectedClass->constructor &&
                ImGui::MenuItem(ICON_FA_WAND_MAGIC_SPARKLES "  Constructor"))
            {
                const int classId = selectedClass->ID.id;
                const int constructorId = m_IDGenerator.GetNextId();
                pendingActions.push_back(std::make_shared<DeferredAction>(
                    [this, classId, constructorId]()
                    {
                        const OperationResult result =
                            m_operations->AddClassConstructor(classId, constructorId);
                        m_fileStatusIsError = !result;
                        m_fileStatus = result ? "Class constructor added" : result.error;
                        if (result) RebuildScriptTree();
                    }));
            }
            ImGui::Separator();
        }

        if (selectedFunction)
        {
            ImGui::TextDisabled("%s ports",
                                selectedFunction->functionDef->name.c_str());
            if (ImGui::MenuItem(ICON_FA_ARROW_RIGHT_TO_BRACKET "  Input"))
                pendingActions.push_back(std::make_shared<AddFunctionInputAction>(
                    this, selectedFunction->ID.id, m_IDGenerator.GetNextId()));
            const ScriptClassPtr owner =
                ScriptUtils::FindOwningClass(m_script, selectedFunction->ID.id);
            const bool isConstructor = owner && owner->constructor == selectedFunction;
            if (!isConstructor &&
                ImGui::MenuItem(ICON_FA_ARROW_RIGHT_FROM_BRACKET "  Output"))
                pendingActions.push_back(std::make_shared<AddFunctionOutputAction>(
                    this, selectedFunction->ID.id, m_IDGenerator.GetNextId()));
            ImGui::Separator();
        }

        ImGui::TextDisabled("Script");
        if (ImGui::MenuItem(ICON_FA_DIAGRAM_PROJECT "  Function"))
            pendingActions.push_back(std::make_shared<AddFunctionAction>(
                this, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem(ICON_FA_DATABASE "  Variable"))
            pendingActions.push_back(std::make_shared<AddVariableAction>(
                this, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem(ICON_FA_CUBES "  Class"))
        {
            const int id = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id]()
            {
                const std::string name = Utils::FindValidName("Class", m_scriptTreeView);
                const OperationResult result = m_operations->AddClass(id, name);
                m_fileStatusIsError = !result;
                m_fileStatus = result ? "Class added" : result.error;
                if (result) RebuildScriptTree();
            }));
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    RenderTreeNode(m_scriptTreeView, m_selectedItemId, m_editingItemId,
                   m_scriptFilter.c_str());

    if (ImGui::BeginPopupContextWindow("Explorer context",
                                       ImGuiPopupFlags_MouseButtonRight))
    {
        if (ImGui::MenuItem(ICON_FA_PLUS "  Add Function"))
            pendingActions.push_back(std::make_shared<AddFunctionAction>(
                this, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem(ICON_FA_PLUS "  Add Variable"))
            pendingActions.push_back(std::make_shared<AddVariableAction>(
                this, m_IDGenerator.GetNextId()));
        ImGui::EndPopup();
    }
}

void Example::ShowInspector()
{
    PanelHeading(HeaderFont(), ICON_FA_SLIDERS, "Inspector");

    const auto queueOperation =
        [this](const char* successMessage, std::function<OperationResult()> operation,
               bool rebuildTree = false)
        {
            pendingActions.push_back(std::make_shared<DeferredAction>(
                [this, successMessage = std::string(successMessage),
                 operation = std::move(operation), rebuildTree]()
                {
                    const OperationResult result = operation();
                    m_fileStatusIsError = !result;
                    m_fileStatus = result ? successMessage : result.error;
                    if (result && rebuildTree)
                        RebuildScriptTree();
                }));
        };

    std::vector<ed::NodeId> selectedNodes(ed::GetSelectedObjectCount());
    std::vector<ed::LinkId> selectedLinks(ed::GetSelectedObjectCount());
    const int nodeCount = ed::GetSelectedNodes(selectedNodes.data(),
                                               static_cast<int>(selectedNodes.size()));
    const int linkCount = ed::GetSelectedLinks(selectedLinks.data(),
                                               static_cast<int>(selectedLinks.size()));

    if (nodeCount == 1)
    {
        if (m_graphView.m_pGraph)
        {
            if (NodePtr node = m_graphView.m_pGraph->FindNode(selectedNodes.front()))
            {
                ImGui::TextDisabled("NODE");
                ImGui::PushFont(HeaderFont());
                ImGui::TextWrapped("%s", node->Name.c_str());
                ImGui::PopFont();
                ImGui::TextDisabled("Graph: %s",
                    m_graphView.m_pScriptFunction
                        ? m_graphView.m_pScriptFunction->functionDef->name.c_str()
                        : "Unknown");

                if (ImGui::CollapsingHeader("Inputs", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (node->Inputs.empty())
                        ImGui::TextDisabled("This node has no inputs.");

                    for (int i = 0; i < static_cast<int>(node->Inputs.size()); ++i)
                    {
                        const Pin& input = node->Inputs[i];
                        ImGui::PushID(i);
                        ImGui::Separator();
                        ImGui::TextUnformatted(input.Name.empty() ? "Input" : input.Name.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", InspectorTypeName(input.Type));

                        const bool linked = m_graphView.m_pGraph->IsPinLinked(input.ID);
                        if (input.Type == PinType::Flow)
                        {
                            ImGui::TextDisabled("Flow connection");
                        }
                        else if (linked)
                        {
                            ImGui::TextDisabled(ICON_FA_LINK " Value supplied by a connection");
                        }
                        else if (i < static_cast<int>(node->InputValues.size()))
                        {
                            Value value = CloneInspectorValue(node->InputValues[i]);
                            if (DrawInspectorValueEditor(
                                    "node-input", value, input.Type == PinType::Any))
                            {
                                const int functionId =
                                    m_graphView.m_pScriptFunction->ID.id;
                                const ed::NodeId nodeId = node->ID;
                                queueOperation("Node input updated",
                                    [this, functionId, nodeId, i, value]()
                                    {
                                        return m_operations->ChangeNodeInputValue(
                                            functionId, nodeId, i, value);
                                    });
                            }
                        }
                        else
                        {
                            ImGui::TextDisabled("No stored default value");
                        }
                        ImGui::PopID();
                    }
                }

                if (ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (node->Outputs.empty())
                        ImGui::TextDisabled("This node has no outputs.");
                    for (const Pin& output : node->Outputs)
                    {
                        ImGui::BulletText("%s", output.Name.empty()
                            ? "Output" : output.Name.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", InspectorTypeName(output.Type));
                    }
                }

                ImGui::Spacing();
                if (ImGui::Button(ICON_FA_CROSSHAIRS " Frame node", ImVec2(-1, 0)))
                    ed::NavigateToSelection(false);
            }
        }
        return;
    }

    if (nodeCount > 1 || linkCount > 0)
    {
        ImGui::TextDisabled("GRAPH SELECTION");
        ImGui::Text("%d node%s", nodeCount, nodeCount == 1 ? "" : "s");
        ImGui::Text("%d link%s", linkCount, linkCount == 1 ? "" : "s");
        if (ImGui::Button(ICON_FA_CROSSHAIRS " Frame selection", ImVec2(-1, 0)))
            ed::NavigateToSelection(false);
        return;
    }

    TreeNode* selected = FindNodeByID(m_selectedItemId);
    if (!selected)
    {
        ImGui::TextDisabled("Nothing selected");
        ImGui::TextWrapped("Select an item in the Script Explorer or a node on the canvas.");
        return;
    }

    ScriptFunctionPtr function =
        std::dynamic_pointer_cast<ScriptFunction>(selected->pElement);
    ScriptClassPtr scriptClass =
        std::dynamic_pointer_cast<ScriptClass>(selected->pElement);
    ScriptPropertyPtr property =
        std::dynamic_pointer_cast<ScriptProperty>(selected->pElement);

    for (TreeNode* ancestor = selected; ancestor && !function && !scriptClass;
         ancestor = ancestor->parentId >= 0 ? FindNodeByID(ancestor->parentId) : nullptr)
    {
        function = std::dynamic_pointer_cast<ScriptFunction>(ancestor->pElement);
        scriptClass = std::dynamic_pointer_cast<ScriptClass>(ancestor->pElement);
    }

    if (scriptClass && !function && !property)
    {
        ImGui::TextDisabled("CLASS");
        ImGui::PushFont(HeaderFont());
        ImGui::TextWrapped("%s", scriptClass->Name.c_str());
        ImGui::PopFont();

        std::string name = scriptClass->Name;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("Name", &name))
        {
            const int classId = scriptClass->ID.id;
            queueOperation("Class renamed",
                [this, classId, name]()
                {
                    return m_operations->RenameClass(classId, name);
                }, true);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("ADD MEMBER");
        if (ImGui::Button(ICON_FA_DATABASE " Property", ImVec2(-1, 0)))
        {
            const int classId = scriptClass->ID.id;
            const int propertyId = m_IDGenerator.GetNextId();
            queueOperation("Class property added",
                [this, classId, propertyId]()
                {
                    return m_operations->AddClassProperty(
                        classId, propertyId, "Property");
                }, true);
        }
        if (ImGui::Button(ICON_FA_DIAGRAM_PROJECT " Method", ImVec2(-1, 0)))
        {
            const int classId = scriptClass->ID.id;
            const int methodId = m_IDGenerator.GetNextId();
            queueOperation("Class method added",
                [this, classId, methodId]()
                {
                    return m_operations->AddClassMethod(classId, methodId, "Method");
                }, true);
        }
        ImGuiUtils::BeginDisabled(scriptClass->constructor != nullptr);
        if (ImGui::Button(ICON_FA_WAND_MAGIC_SPARKLES " Constructor", ImVec2(-1, 0)))
        {
            const int classId = scriptClass->ID.id;
            const int constructorId = m_IDGenerator.GetNextId();
            queueOperation("Class constructor added",
                [this, classId, constructorId]()
                {
                    return m_operations->AddClassConstructor(classId, constructorId);
                }, true);
        }
        ImGuiUtils::EndDisabled();
        if (scriptClass->constructor)
            ImGui::TextDisabled("This class already has a constructor.");

        ImGui::Spacing();
        ImGui::Text("%zu propert%s", scriptClass->properties.size(),
                    scriptClass->properties.size() == 1 ? "y" : "ies");
        ImGui::Text("%zu method%s", scriptClass->methods.size(),
                    scriptClass->methods.size() == 1 ? "" : "s");
        return;
    }

    if (property)
    {
        const ScriptClassPtr owner =
            ScriptUtils::FindOwningClass(m_script, property->ID.id);
        const bool isClassProperty = owner != nullptr;
        ImGui::TextDisabled(isClassProperty ? "CLASS PROPERTY" : "VARIABLE");
        ImGui::PushFont(HeaderFont());
        ImGui::TextWrapped("%s", property->Name.c_str());
        ImGui::PopFont();
        if (owner)
            ImGui::TextDisabled("Class: %s", owner->Name.c_str());

        std::string name = property->Name;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("Name", &name))
        {
            const int propertyId = property->ID.id;
            if (owner)
            {
                const int classId = owner->ID.id;
                queueOperation("Property renamed",
                    [this, classId, propertyId, name]()
                    {
                        return m_operations->RenameClassProperty(
                            classId, propertyId, name);
                    }, true);
            }
            else
            {
                queueOperation("Variable renamed",
                    [this, propertyId, name]()
                    {
                        return m_operations->RenameVariable(propertyId, name);
                    }, true);
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("DEFAULT VALUE");
        Value value = CloneInspectorValue(property->defaultValue);
        if (DrawInspectorValueEditor("property-value", value))
        {
            const int propertyId = property->ID.id;
            if (owner)
            {
                const int classId = owner->ID.id;
                queueOperation("Property value updated",
                    [this, classId, propertyId, value]()
                    {
                        return m_operations->ChangeClassPropertyValue(
                            classId, propertyId, value);
                    });
            }
            else
            {
                queueOperation("Variable value updated",
                    [this, propertyId, value]()
                    {
                        return m_operations->ChangeVariableValue(propertyId, value);
                    });
            }
        }
        return;
    }

    if (function)
    {
        const ScriptClassPtr owner =
            ScriptUtils::FindOwningClass(m_script, function->ID.id);
        const bool isConstructor = owner && owner->constructor == function;
        ImGui::TextDisabled(isConstructor ? "CONSTRUCTOR"
                                          : owner ? "METHOD" : "FUNCTION");
        ImGui::PushFont(HeaderFont());
        ImGui::TextWrapped("%s", isConstructor ? owner->Name.c_str()
                                                : function->functionDef->name.c_str());
        ImGui::PopFont();
        if (owner)
            ImGui::TextDisabled("Class: %s", owner->Name.c_str());

        if (!isConstructor)
        {
            std::string name = function->functionDef->name;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("Name", &name))
            {
                const int functionId = function->ID.id;
                queueOperation("Function renamed",
                    [this, functionId, name]()
                    {
                        return m_operations->RenameFunction(functionId, name);
                    }, true);
            }
        }

        const int functionId = function->ID.id;
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Inputs", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Button(ICON_FA_PLUS " Add input", ImVec2(-1, 0)))
                pendingActions.push_back(std::make_shared<AddFunctionInputAction>(
                    this, functionId, m_IDGenerator.GetNextId()));

            if (function->functionDef->inputs.empty())
                ImGui::TextDisabled("No inputs.");

            for (const BasicFunctionDef::Input& input : function->functionDef->inputs)
            {
                ImGui::PushID(input.id);
                ImGui::Separator();
                std::string inputName = input.name;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##input-name", &inputName))
                {
                    queueOperation("Input renamed",
                        [this, functionId, inputId = input.id, inputName]()
                        {
                            return m_operations->RenameFunctionInput(
                                functionId, inputId, inputName);
                        }, true);
                }
                Value value = CloneInspectorValue(input.value);
                if (DrawInspectorValueEditor("input-default", value))
                {
                    queueOperation("Input default updated",
                        [this, functionId, inputId = input.id, value]()
                        {
                            return m_operations->ChangeFunctionInputValue(
                                functionId, inputId, value);
                        });
                }
                if (ImGui::Button(ICON_FA_TRASH_CAN " Remove input"))
                {
                    queueOperation("Input removed",
                        [this, functionId, inputId = input.id]()
                        {
                            return m_operations->RemoveFunctionInput(
                                functionId, inputId);
                        }, true);
                }
                ImGui::PopID();
            }
        }

        if (!isConstructor &&
            ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Button(ICON_FA_PLUS " Add output", ImVec2(-1, 0)))
                pendingActions.push_back(std::make_shared<AddFunctionOutputAction>(
                    this, functionId, m_IDGenerator.GetNextId()));

            if (function->functionDef->outputs.empty())
                ImGui::TextDisabled("No outputs.");

            for (const BasicFunctionDef::Input& output : function->functionDef->outputs)
            {
                ImGui::PushID(output.id);
                ImGui::Separator();
                std::string outputName = output.name;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##output-name", &outputName))
                {
                    queueOperation("Output renamed",
                        [this, functionId, outputId = output.id, outputName]()
                        {
                            return m_operations->RenameFunctionOutput(
                                functionId, outputId, outputName);
                        }, true);
                }
                Value value = CloneInspectorValue(output.value);
                if (DrawInspectorValueEditor("output-default", value))
                {
                    queueOperation("Output default updated",
                        [this, functionId, outputId = output.id, value]()
                        {
                            return m_operations->ChangeFunctionOutputValue(
                                functionId, outputId, value);
                        });
                }
                if (ImGui::Button(ICON_FA_TRASH_CAN " Remove output"))
                {
                    queueOperation("Output removed",
                        [this, functionId, outputId = output.id]()
                        {
                            return m_operations->RemoveFunctionOutput(
                                functionId, outputId);
                        }, true);
                }
                ImGui::PopID();
            }
        }
        return;
    }

    ImGui::TextDisabled("SCRIPT");
    ImGui::PushFont(HeaderFont());
    ImGui::TextWrapped("%s", selected->label.c_str());
    ImGui::PopFont();
    ImGui::TextWrapped(
        "Select a class, function, variable, property, port, or graph node "
        "to edit its details.");
}

void Example::SetBottomPanel(BottomPanelTab tab)
{
    m_bottomPanelTab = tab;
    m_selectBottomPanelTab = true;
    m_showBottomPanel = true;
}

void Example::FocusDiagnostic(const ValidationDiagnostic& diagnostic)
{
    ScriptFunctionPtr function;
    if (m_script.main && diagnostic.functionId == m_script.main->ID)
        function = m_script.main;
    else
        function = ScriptUtils::FindFunctionById(m_script, diagnostic.functionId.id);

    if (function && function != m_graphView.m_pScriptFunction)
        ChangeGraph(function);

    if (diagnostic.nodeId)
    {
        ed::ClearSelection();
        ed::SelectNode(diagnostic.nodeId);
        ed::NavigateToSelection(false);
    }
}

void Example::ShowProblemsPanel()
{
    if (m_validationReport.diagnostics.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kSuccess);
        ImGui::TextUnformatted(ICON_FA_CIRCLE_CHECK);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted("No problems found.");
        return;
    }

    if (ImGui::BeginTable("ProblemsTable", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < m_validationReport.diagnostics.size(); ++i)
        {
            const ValidationDiagnostic& diagnostic = m_validationReport.diagnostics[i];
            const bool error = diagnostic.severity == DiagnosticSeverity::Error;
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, error ? kError : kWarning);
            ImGui::TextUnformatted(error ? ICON_FA_CIRCLE_XMARK : ICON_FA_TRIANGLE_EXCLAMATION);
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Selectable(diagnostic.message.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns))
                FocusDiagnostic(diagnostic);
            Tooltip("Open and frame the affected node");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", diagnostic.graphName.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void Example::ShowOutputPanel()
{
    if (ImGui::Button(ICON_FA_TRASH_CAN " Clear"))
        m_runOutput.clear();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLAY " Run again"))
        CompileScript(true);
    ImGui::Separator();

    if (m_runOutput.empty())
        ImGui::TextDisabled("No output.");
    else
        ImGui::TextUnformatted(m_runOutput.c_str());
}

void Example::ShowDeveloperPanel()
{
    static bool showStyleEditor = false;

    if (ImGui::Button(ICON_FA_CROSSHAIRS " Frame all"))
        ed::NavigateToContent();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_WAND_MAGIC_SPARKLES " Show flow"))
        for (auto& link : m_graphView.m_pGraph->GetLinks())
            ed::Flow(link.ID);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PALETTE " Style editor"))
        showStyleEditor = true;

    if (showStyleEditor)
        ShowStyleEditor(&showStyleEditor);

    ShowCompilerInfo(ImGui::GetContentRegionAvail().x);
    if (ImGui::CollapsingHeader("Selection internals"))
        ShowNodeSelection(ImGui::GetContentRegionAvail().x);
}

void Example::ShowBottomPanel()
{
    if (ImGui::BeginTabBar("BottomPanelTabs", ImGuiTabBarFlags_Reorderable))
    {
        char problemsLabel[96];
        snprintf(problemsLabel, sizeof(problemsLabel), ICON_FA_LIST_CHECK " Problems  %zu",
                 m_validationReport.diagnostics.size());
        ImGuiTabItemFlags problemFlags =
            m_selectBottomPanelTab && m_bottomPanelTab == BottomPanelTab::Problems
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(problemsLabel, nullptr, problemFlags))
        {
            m_bottomPanelTab = BottomPanelTab::Problems;
            ShowProblemsPanel();
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags outputFlags =
            m_selectBottomPanelTab && m_bottomPanelTab == BottomPanelTab::Output
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(ICON_FA_TERMINAL " Output", nullptr, outputFlags))
        {
            m_bottomPanelTab = BottomPanelTab::Output;
            ShowOutputPanel();
            ImGui::EndTabItem();
        }

        if (m_showDeveloperTools)
        {
            ImGuiTabItemFlags developerFlags =
                m_selectBottomPanelTab && m_bottomPanelTab == BottomPanelTab::Developer
                    ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(ICON_FA_BUG " Developer", nullptr, developerFlags))
            {
                m_bottomPanelTab = BottomPanelTab::Developer;
                ShowDeveloperPanel();
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }
    m_selectBottomPanelTab = false;
}

void Example::CompileScript(bool runAfterCompile)
{
    VM& vm = VM::getInstance();
    Utils::CaptureStdout captureCompilation;
    std::cout << "Compiling script...\n";

    ScriptCompileOptions compileOptions;
    compileOptions.enableConstantFolding = m_isConstFoldingEnabled;
    compileOptions.disassemble = m_showDeveloperTools;
    const ScriptCompileResult compileResult =
        ScriptRuntime::Compile(vm, m_script, compileOptions);

    m_validationReport = compileResult.validation;
    m_constFoldingValues = compileResult.foldedValues;
    m_constFoldingIDs = compileResult.foldedNodeIds;
    m_compileOutput = captureCompilation.Restore();

    if (!compileResult.function || m_validationReport.HasErrors())
    {
        m_fileStatus = "Compilation failed";
        m_fileStatusIsError = true;
        SetBottomPanel(BottomPanelTab::Problems);
        return;
    }

    if (!runAfterCompile)
    {
        m_fileStatus = "Compiled successfully";
        m_fileStatusIsError = false;
        if (m_compileOutput.empty())
            m_compileOutput = "Compilation completed successfully.";
        return;
    }

    Utils::CaptureStdout captureExecution;
    const InterpretResult executionResult =
        ScriptRuntime::Execute(vm, compileResult.function);
    m_runOutput = captureExecution.Restore();

    if (executionResult == InterpretResult::INTERPRET_OK)
    {
        m_fileStatus = "Run completed";
        m_fileStatusIsError = false;
        if (m_runOutput.empty())
            m_runOutput = "Program completed with no output.";
    }
    else
    {
        m_fileStatus = executionResult == InterpretResult::INTERPRET_COMPILE_ERROR
            ? "Run stopped: compilation error"
            : "Run stopped: runtime error";
        m_fileStatusIsError = true;
        if (m_runOutput.empty())
            m_runOutput = m_fileStatus;
    }

    SetBottomPanel(BottomPanelTab::Output);
}

void Example::DrawMenuBar()
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open...", "Ctrl+O"))
            if (const std::optional<std::string> path =
                    SelectVloxFile(false, m_currentScriptPath))
                LoadScript(*path);
        if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save", "Ctrl+S"))
        {
            if (!m_currentScriptPath.empty())
                SaveScript(m_currentScriptPath);
            else if (const std::optional<std::string> path =
                    SelectVloxFile(true, "Untitled.vlox"))
                SaveScript(*path);
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
        {
            const std::string suggested =
                m_currentScriptPath.empty() ? "Untitled.vlox" : m_currentScriptPath;
            if (const std::optional<std::string> path =
                    SelectVloxFile(true, suggested))
                SaveScript(*path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            Quit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Undo", "Ctrl+Z", false, CanUndo()))
            UndoLastAction();
        if (ImGui::MenuItem(ICON_FA_ROTATE_RIGHT "  Redo", "Ctrl+Y", false, CanRedo()))
            RedoLastAction();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_COPY "  Copy", "Ctrl+C"))
            CopySelection();
        if (ImGui::MenuItem(ICON_FA_PASTE "  Paste", "Ctrl+V"))
            PasteClipboard();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Script Explorer", nullptr, &m_showScriptExplorer);
        ImGui::MenuItem("Inspector", nullptr, &m_showInspector);
        ImGui::MenuItem("Bottom Panel", nullptr, &m_showBottomPanel);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_CROSSHAIRS "  Frame All", "Home"))
            ed::NavigateToContent();
        ImGui::MenuItem("Developer Tools", nullptr, &m_showDeveloperTools);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Run"))
    {
        if (ImGui::MenuItem(ICON_FA_CODE "  Compile", "Ctrl+Enter"))
            CompileScript(false);
        if (ImGui::MenuItem(ICON_FA_PLAY "  Run", "F5"))
            CompileScript(true);
        ImGui::Separator();
        ImGui::MenuItem("Validate as you edit", nullptr, &m_isRealTimeCompilationEnabled);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem(ICON_FA_CIRCLE_QUESTION "  Quick Guide", "F1"))
            m_showHelp = true;
        ImGui::Separator();
        if (ImGui::MenuItem("About Visual Lox"))
            m_showAbout = true;
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();

    if (m_showHelp)
    {
        ImGui::SetNextWindowSize(ImVec2(720.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(ICON_FA_CIRCLE_QUESTION "  Visual Lox Help", &m_showHelp))
        {
            ImGui::PushFont(HeaderFont());
            ImGui::TextUnformatted("Build scripts visually");
            ImGui::PopFont();
            ImGui::TextWrapped(
                "Visual Lox is organized around a Script Explorer, a node canvas, "
                "an Inspector, and the Problems/Output panel. Select an item to edit "
                "its details in the Inspector.");
            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Getting started", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::BulletText("Use the + button in Script Explorer to create functions, variables, and classes.");
                ImGui::BulletText("Select a class before opening + to add properties, methods, or its constructor.");
                ImGui::BulletText("Select a function or method before opening + to add inputs and outputs.");
                ImGui::BulletText("Open a graph, then press Space or right-click the canvas to add nodes.");
                ImGui::BulletText("Drag from an output pin to a compatible input pin to connect nodes.");
            }

            if (ImGui::CollapsingHeader("Inspector", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::BulletText("Node: edit unconnected input values and inspect output types.");
                ImGui::BulletText("Variable or property: edit its name, type, and default value.");
                ImGui::BulletText("Function or method: edit its name, inputs, outputs, and defaults.");
                ImGui::BulletText("Lists and long strings use expanded editors that do not need to fit on a node.");
            }

            if (ImGui::CollapsingHeader("Classes"))
            {
                ImGui::BulletText("A class can contain one constructor, any number of methods, and properties.");
                ImGui::BulletText("Constructors accept inputs but do not expose user-defined outputs.");
                ImGui::BulletText("Double-click an Explorer label to rename it; right-click for delete and member actions.");
            }

            if (ImGui::CollapsingHeader("Run and diagnose"))
            {
                ImGui::BulletText("Compile checks the script without running it.");
                ImGui::BulletText("Run compiles and executes the script; output appears in the Output tab.");
                ImGui::BulletText("Click a row in Problems to open and frame the affected graph node.");
            }

            if (ImGui::CollapsingHeader("Keyboard and navigation"))
            {
                if (ImGui::BeginTable("Help shortcuts", 2,
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
                {
                    ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                    const std::pair<const char*, const char*> shortcuts[] = {
                        { "Space / right-click", "Add a node on the canvas" },
                        { "Home", "Frame all nodes" },
                        { "Ctrl+Enter", "Compile" },
                        { "F5", "Run" },
                        { "Ctrl+S", "Save" },
                        { "Ctrl+O", "Open" },
                        { "Ctrl+Z / Ctrl+Y", "Undo / redo" },
                        { "Ctrl+C / Ctrl+V", "Copy / paste" },
                        { "Delete", "Delete the selected graph item" },
                        { "F1", "Open this guide" },
                    };
                    for (const auto& shortcut : shortcuts)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(shortcut.first);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(shortcut.second);
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::End();
    }

    if (m_showAbout)
        ImGui::OpenPopup("About Visual Lox");
    if (ImGui::BeginPopupModal("About Visual Lox", &m_showAbout,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushFont(HeaderFont());
        ImGui::TextUnformatted(ICON_FA_DIAGRAM_PROJECT "  Visual Lox");
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::TextUnformatted("A visual scripting language based on Lox.");
        ImGui::TextDisabled("Create scripts by connecting typed nodes.");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(100.0f, 0)))
            m_showAbout = false;
        ImGui::EndPopup();
    }
}

void Example::DrawToolbar()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.084f, 0.106f, 1.0f));
    ImGui::BeginChild("Main Toolbar", ImVec2(0, 44.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();

    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open"))
        if (const std::optional<std::string> path =
                SelectVloxFile(false, m_currentScriptPath))
            LoadScript(*path);
    Tooltip("Open a Visual Lox script (Ctrl+O)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save"))
    {
        if (!m_currentScriptPath.empty())
            SaveScript(m_currentScriptPath);
        else if (const std::optional<std::string> path =
                SelectVloxFile(true, "Untitled.vlox"))
            SaveScript(*path);
    }
    Tooltip("Save the current script (Ctrl+S)");

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(5.0f, 0));
    ImGui::SameLine();
    ImGuiUtils::BeginDisabled(!CanUndo());
    if (ImGui::Button(ICON_FA_ROTATE_LEFT "##undo"))
        UndoLastAction();
    ImGuiUtils::EndDisabled();
    Tooltip("Undo (Ctrl+Z)");
    ImGui::SameLine();
    ImGuiUtils::BeginDisabled(!CanRedo());
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT "##redo"))
        RedoLastAction();
    ImGuiUtils::EndDisabled();
    Tooltip("Redo (Ctrl+Y)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CROSSHAIRS "##frameAll"))
        ed::NavigateToContent();
    Tooltip("Frame all nodes (Home)");

    const float compileWidth =
        ImGui::CalcTextSize(ICON_FA_CODE " Compile").x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
    const float runWidth =
        ImGui::CalcTextSize(ICON_FA_PLAY " Run").x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
    const float actionsWidth = compileWidth + runWidth + ImGui::GetStyle().ItemSpacing.x;
    const float rightX = ImGui::GetWindowWidth() - actionsWidth -
                         ImGui::GetStyle().WindowPadding.x;
    if (rightX > ImGui::GetCursorPosX())
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(rightX);
    }
    if (ImGui::Button(ICON_FA_CODE " Compile"))
        CompileScript(false);
    Tooltip("Compile the current script (Ctrl+Enter)");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.55f, 0.34f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.66f, 0.41f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.13f, 0.47f, 0.29f, 1.0f));
    if (ImGui::Button(ICON_FA_PLAY " Run"))
        CompileScript(true);
    ImGui::PopStyleColor(3);
    Tooltip("Compile and run (F5)");

    ImGui::EndChild();
}

void Example::DrawStatusBar()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.065f, 0.073f, 0.092f, 1.0f));
    ImGui::BeginChild("Status Bar", ImVec2(0, 27.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();

    const std::string documentName = m_currentScriptPath.empty()
        ? "Untitled.vlox"
        : std::filesystem::path(m_currentScriptPath).filename().string();
    ImGui::TextDisabled("%s", documentName.c_str());
    if (!m_fileStatus.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, m_fileStatusIsError ? kError : kMuted);
        ImGui::TextUnformatted(m_fileStatus.c_str());
        ImGui::PopStyleColor();
    }

    char rightStatus[160];
    snprintf(rightStatus, sizeof(rightStatus), "%zu error%s  %zu warning%s    %.0f%%",
             m_validationReport.ErrorCount(),
             m_validationReport.ErrorCount() == 1 ? "" : "s",
             m_validationReport.WarningCount(),
             m_validationReport.WarningCount() == 1 ? "" : "s",
             ed::GetCurrentZoom() * 100.0f);
    const float rightWidth = ImGui::CalcTextSize(rightStatus).x;
    ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + 20.0f,
                          ImGui::GetWindowContentRegionMax().x - rightWidth));
    ImGui::TextDisabled("%s", rightStatus);
    ImGui::EndChild();
}

void Example::OnFrame(float deltaTime)
{
    // Pending actions
    for (auto& action : pendingActions)
    {
        action->Run();
    }

    pendingActions.clear();
    if (m_commitPendingEdit)
    {
        if (m_operations->IsTransactionActive())
        {
            const OperationResult result = m_operations->CommitTransaction();
            m_fileStatusIsError = !result;
            if (!result) m_fileStatus = result.error;
        }
        m_commitPendingEdit = false;
    }

    m_graphView.OnFrame(deltaTime);

    const bool editingText = GImGui && GImGui->InputTextState.ID != 0;
    if (!editingText && ImGui::GetIO().KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C), false))
            CopySelection();
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V), false))
            PasteClipboard();
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z), false))
        {
            if (ImGui::GetIO().KeyShift)
            {
                if (CanRedo()) RedoLastAction();
            }
            else if (CanUndo())
            {
                UndoLastAction();
            }
        }
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Y), false) && CanRedo())
            RedoLastAction();
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_S), false))
        {
            if (ImGui::GetIO().KeyShift)
            {
                const std::string suggested =
                    m_currentScriptPath.empty() ? "Untitled.vlox" : m_currentScriptPath;
                if (const std::optional<std::string> path =
                        SelectVloxFile(true, suggested))
                    SaveScript(*path);
            }
            else if (!m_currentScriptPath.empty())
                SaveScript(m_currentScriptPath);
            else if (const std::optional<std::string> path =
                    SelectVloxFile(true, "Untitled.vlox"))
                SaveScript(*path);
        }
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_O), false))
            if (const std::optional<std::string> path =
                    SelectVloxFile(false, m_currentScriptPath))
                LoadScript(*path);
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false))
            CompileScript(false);
    }

    if (!editingText && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F5), false))
        CompileScript(true);
    if (!editingText && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F1), false))
        m_showHelp = true;
    if (!editingText && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home), false))
        ed::NavigateToContent();

    m_validationReport = ScriptValidator::Validate(m_script);
    m_graphView.validationReport = &m_validationReport;

    VM& vm = VM::getInstance();
    Compiler& compiler = vm.getCompiler();

    // Traverse graph to see which nodes are processed, in order to display them enabled in the graph view
    if (m_validationReport.HasErrors())
        m_graphView.processedNodes.clear();
    else
        m_graphView.processedNodes = GatherProcessedNodes(*m_graphView.m_pGraph, compiler);

    DrawMenuBar();
    DrawToolbar();

    const float statusHeight = 27.0f;
    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float verticalSpacing = ImGui::GetStyle().ItemSpacing.y;
    const float bottomSplitterHeight = m_showBottomPanel ? 5.0f : 0.0f;
    const float layoutSpacing = m_showBottomPanel ? verticalSpacing * 3.0f
                                                   : verticalSpacing;
    const float usableHeight = ImMax(240.0f, availableHeight - statusHeight -
                                              layoutSpacing);
    const float maxBottomHeight = ImMax(160.0f, usableHeight - 260.0f);
    m_bottomPaneHeight = ImClamp(m_bottomPaneHeight, 160.0f, maxBottomHeight);
    const float mainHeight = m_showBottomPanel
        ? usableHeight - m_bottomPaneHeight - bottomSplitterHeight
        : usableHeight;

    ImGui::BeginChild("Workspace", ImVec2(0, mainHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float workspaceWidth = ImGui::GetContentRegionAvail().x;
    const int visibleSidePanels = (m_showScriptExplorer ? 1 : 0) + (m_showInspector ? 1 : 0);
    const float splitterWidth = visibleSidePanels * 5.0f;
    const float minimumCanvasWidth = 360.0f;
    if (m_showScriptExplorer)
        m_leftPaneWidth = ImClamp(m_leftPaneWidth, 220.0f,
            ImMax(220.0f, workspaceWidth - minimumCanvasWidth -
                            (m_showInspector ? m_rightPaneWidth : 0.0f) - splitterWidth));
    if (m_showInspector)
        m_rightPaneWidth = ImClamp(m_rightPaneWidth, 240.0f,
            ImMax(240.0f, workspaceWidth - minimumCanvasWidth -
                            (m_showScriptExplorer ? m_leftPaneWidth : 0.0f) - splitterWidth));

    if (m_showScriptExplorer)
    {
        ImGui::BeginChild("Script Explorer Panel", ImVec2(m_leftPaneWidth, mainHeight), true);
        ShowScriptExplorer();
        ImGui::EndChild();
        ImGui::SameLine(0, 0);
        const float maxLeft = workspaceWidth - minimumCanvasWidth -
            (m_showInspector ? m_rightPaneWidth + 5.0f : 0.0f);
        DrawVerticalSplitter("##ScriptExplorerSplitter", m_leftPaneWidth,
                             220.0f, ImMax(220.0f, maxLeft), mainHeight);
        ImGui::SameLine(0, 0);
    }

    const float remainingWidth = ImGui::GetContentRegionAvail().x;
    const float centerWidth = ImMax(minimumCanvasWidth,
        remainingWidth - (m_showInspector ? m_rightPaneWidth + 5.0f : 0.0f));
    ImGui::BeginChild("Graph Canvas Panel", ImVec2(centerWidth, mainHeight), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const char* graphTitle = m_graphView.m_pScriptFunction
        ? m_graphView.m_pScriptFunction->functionDef->name.c_str()
        : "Graph";
    PanelHeading(HeaderFont(), ICON_FA_DIAGRAM_PROJECT, graphTitle);

    m_graphView.DrawNodeEditor(m_HeaderBackground, 0, 0);

    auto editorMin = ImGui::GetItemRectMin();
    auto editorMax = ImGui::GetItemRectMax();

    if (m_graphView.m_pGraph && m_graphView.m_pGraph->GetNodes().size() <= 1)
    {
        const char* hint = ICON_FA_PLUS "  Press Space or right-click to add a node";
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        const ImVec2 hintPosition(
            (editorMin.x + editorMax.x - hintSize.x) * 0.5f,
            editorMin.y + 20.0f);
        ImGui::GetWindowDrawList()->AddText(
            hintPosition, ImGui::GetColorU32(kMuted), hint);
    }

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

    ImGui::EndChild();

    if (m_showInspector)
    {
        ImGui::SameLine(0, 0);
        DrawVerticalSplitter("##InspectorSplitter", m_rightPaneWidth,
                             240.0f, 480.0f, mainHeight, true);
        ImGui::SameLine(0, 0);
        ImGui::BeginChild("Inspector Panel", ImVec2(0, mainHeight), true);
        ShowInspector();
        ImGui::EndChild();
    }

    ImGui::EndChild();

    if (m_showBottomPanel)
    {
        DrawHorizontalSplitter("##BottomPanelSplitter", m_bottomPaneHeight,
                               160.0f, maxBottomHeight,
                               ImGui::GetContentRegionAvail().x);
        ImGui::BeginChild("Bottom Panel", ImVec2(0, m_bottomPaneHeight), true);
        ShowBottomPanel();
        ImGui::EndChild();
    }

    DrawStatusBar();


}

TreeNode Example::MakeFunctionNode(int funId, const std::string& name)
{
    TreeNode funcNode;
    funcNode.id = funId;
    funcNode.icon = m_FunctionIcon;
    funcNode.iconText = ICON_FA_DIAGRAM_PROJECT;
    funcNode.label = name;
    funcNode.onclick = [this, funId]()
    {
        ed::ClearSelection();
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
    varNode.iconText = ICON_FA_DATABASE;
    varNode.id = varId;
    varNode.onclick = []() { ed::ClearSelection(); };
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
            const bool valueChanged = GraphViewUtils::DrawTypeInput(TypeOfValue(tmp), tmp);
            if (ImGui::IsItemActivated() && !m_operations->IsTransactionActive())
                m_operations->BeginTransaction("Edit variable value");
            if (valueChanged)
            {
                pendingActions.push_back(std::make_shared<ChangeVariableValueAction>(this, varId, tmp));
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_commitPendingEdit = true;
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
    inputNode.iconText = ICON_FA_ARROW_RIGHT_TO_BRACKET;
    inputNode.label = name;
    inputNode.onclick = []() { ed::ClearSelection(); };
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
                const bool valueChanged = GraphViewUtils::DrawTypeInput(TypeOfValue(tmp), tmp);
                if (ImGui::IsItemActivated() && !m_operations->IsTransactionActive())
                    m_operations->BeginTransaction("Edit function input value");
                if (valueChanged)
                {
                    pendingActions.push_back(std::make_shared<ChangeFunctionInputValueAction>(this, funId, inputId, tmp));
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_commitPendingEdit = true;
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
    outputNode.iconText = ICON_FA_ARROW_RIGHT_FROM_BRACKET;
    outputNode.label = name;
    outputNode.onclick = []() { ed::ClearSelection(); };
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
                        pendingActions.push_back(std::make_shared<DeleteFunctionOutputAction>(this, funId, outputId, pOutput->name.c_str(), pOutput->value));
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
                const bool valueChanged = GraphViewUtils::DrawTypeInput(TypeOfValue(tmp), tmp);
                if (ImGui::IsItemActivated() && !m_operations->IsTransactionActive())
                    m_operations->BeginTransaction("Edit function output value");
                if (valueChanged)
                {
                    pendingActions.push_back(std::make_shared<ChangeFunctionOutputValueAction>(this, funId, outputId, tmp));
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_commitPendingEdit = true;
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
    const OperationResult result = m_operations->AddFunction(funId, namestr);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Function added" : result.error;
    if (result) RebuildScriptTree();
}

void Example::AddFunction(const ScriptFunctionPtr& pExistingFunction)
{
    if (pExistingFunction)
        AddFunction(pExistingFunction->ID);
}

void Example::AddVariable(int varId)
{
    const std::string namestr = Utils::FindValidName("Variable", m_scriptTreeView);
    const OperationResult result = m_operations->AddVariable(varId, namestr);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Variable added" : result.error;
    if (result) RebuildScriptTree();
}

void Example::AddVariable(const ScriptPropertyPtr& pVariable)
{
    if (!pVariable) return;
    const OperationResult result = m_operations->AddVariable(pVariable->ID, pVariable->Name, pVariable->defaultValue);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Variable added" : result.error;
    if (result) RebuildScriptTree();
}

void Example::ChangeVariableValue(int id, Value& value)
{
    const OperationResult result = m_operations->ChangeVariableValue(id, value);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
}

void Example::RenameFunction(int funId, const char* name)
{
    const OperationResult result = m_operations->RenameFunction(funId, name);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::RenameVariable(int varId, const char* name)
{
    const OperationResult result = m_operations->RenameVariable(varId, name);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::AddFunctionInput(int funId, int inputId)
{
    TreeNode* pFunNode = FindNodeByID(funId);
    const std::string namestr = pFunNode ? Utils::FindValidName("Input", *pFunNode) : "Input";
    const OperationResult result = m_operations->AddFunctionInput(funId, inputId, namestr);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::AddFunctionInput(int funId, int inputId, const char* name, const Value& value)
{
    const OperationResult result = m_operations->AddFunctionInput(funId, inputId, name, value);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::ChangeFunctionInputValue(int funId, int inputId, Value& value)
{
    const OperationResult result = m_operations->ChangeFunctionInputValue(funId, inputId, value);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
}

void Example::RenameFunctionInput(int funId, int inputId, const char* name)
{
    const OperationResult result = m_operations->RenameFunctionInput(funId, inputId, name);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::AddFunctionOutput(int funId, int outputId)
{
    TreeNode* pFunNode = FindNodeByID(funId);
    const std::string namestr = pFunNode ? Utils::FindValidName("Output", *pFunNode) : "Output";
    const OperationResult result = m_operations->AddFunctionOutput(funId, outputId, namestr);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::AddFunctionOutput(int funId, int outputId, const char* name, const Value& value)
{
    const OperationResult result = m_operations->AddFunctionOutput(funId, outputId, name, value);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::ChangeFunctionOutputValue(int funId, int outputId, Value& value)
{
    const OperationResult result = m_operations->ChangeFunctionOutputValue(funId, outputId, value);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
}

void Example::RenameFunctionOutput(int funId, int outputId, const char* name)
{
    const OperationResult result = m_operations->RenameFunctionOutput(funId, outputId, name);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::RemoveFunction(int funId)
{
    if (m_graphView.m_pScriptFunction && m_graphView.m_pScriptFunction->ID == funId)
        ChangeGraph(m_script.main);
    const OperationResult result = m_operations->RemoveFunction(funId);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Function deleted" : result.error;
    if (result) RebuildScriptTree();
}

void Example::RemoveVariable(int id)
{
    const OperationResult result = m_operations->RemoveVariable(id);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Variable deleted" : result.error;
    if (result) RebuildScriptTree();
}

void Example::RemoveFunctionInput(int funId, int inputId)
{
    const OperationResult result = m_operations->RemoveFunctionInput(funId, inputId);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::RemoveFunctionOutput(int funId, int outputId)
{
    const OperationResult result = m_operations->RemoveFunctionOutput(funId, outputId);
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    else RebuildScriptTree();
}

void Example::CopySelection()
{
    std::vector<ed::NodeId> selected(ed::GetSelectedObjectCount());
    const int count = ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()));
    if (count > 0 && m_graphView.m_pScriptFunction)
    {
        std::vector<int> ids;
        ids.reserve(count);
        for (int i = 0; i < count; ++i) ids.push_back(selected[i].Get());
        const OperationResult result = m_operations->CopyNodes(m_graphView.m_pScriptFunction->ID.id, ids);
        m_fileStatusIsError = !result;
        m_fileStatus = result ? "Copied nodes" : result.error;
        return;
    }

    const OperationResult result = m_operations->CopyScriptElement(m_selectedItemId);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Copied script data" : result.error;
}

void Example::PasteClipboard()
{
    if (!m_operations->HasClipboard())
        return;

    if (m_operations->ClipboardContainsNodes())
    {
        if (!m_graphView.m_pScriptFunction) return;
        const int functionId = m_graphView.m_pScriptFunction->ID.id;
        std::vector<int> pasted;
        const OperationResult result = m_operations->PasteNodes(functionId, pasted);
        m_fileStatusIsError = !result;
        m_fileStatus = result ? "Pasted nodes" : result.error;
        if (result)
        {
            ScriptFunctionPtr function = functionId == m_script.main->ID.id
                ? m_script.main : ScriptUtils::FindFunctionById(m_script, functionId);
            m_graphView.SetGraph(&m_script, function, &function->Graph);
            bool append = false;
            for (int id : pasted)
            {
                ed::SelectNode(ed::NodeId(id), append);
                append = true;
            }
        }
        return;
    }

    int targetFunctionId = m_graphView.m_pScriptFunction
        ? m_graphView.m_pScriptFunction->ID.id : m_script.main->ID.id;
    if (TreeNode* selected = FindNodeByID(m_selectedItemId))
    {
        if ((m_script.main && m_script.main->ID == selected->id) ||
            ScriptUtils::FindFunctionById(m_script, selected->id))
            targetFunctionId = selected->id;
        else if ((m_script.main && m_script.main->ID == selected->parentId) ||
                 ScriptUtils::FindFunctionById(m_script, selected->parentId))
            targetFunctionId = selected->parentId;
    }

    int pastedId = 0;
    const OperationResult result = m_operations->PasteScriptElement(targetFunctionId, pastedId);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Pasted script data" : result.error;
    if (result)
    {
        RebuildScriptTree();
        m_selectedItemId = pastedId;
    }
}

void Example::DoAction(IActionPtr action)
{
    (void)action;
}

void Example::UndoLastAction()
{
    const int functionId = m_graphView.m_pScriptFunction ? m_graphView.m_pScriptFunction->ID.id : 0;
    m_graphView.Destroy();
    const OperationResult result = m_operations->Undo();
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    RebuildScriptTree();
    ScriptFunctionPtr function = functionId == m_script.main->ID.id
        ? m_script.main : ScriptUtils::FindFunctionById(m_script, functionId);
    if (!function) function = m_script.main;
    m_graphView.SetGraph(&m_script, function, &function->Graph);
}

void Example::RedoLastAction()
{
    const int functionId = m_graphView.m_pScriptFunction ? m_graphView.m_pScriptFunction->ID.id : 0;
    m_graphView.Destroy();
    const OperationResult result = m_operations->Redo();
    m_fileStatusIsError = !result;
    if (!result) m_fileStatus = result.error;
    RebuildScriptTree();
    ScriptFunctionPtr function = functionId == m_script.main->ID.id
        ? m_script.main : ScriptUtils::FindFunctionById(m_script, functionId);
    if (!function) function = m_script.main;
    m_graphView.SetGraph(&m_script, function, &function->Graph);
}

bool Example::CanUndo() const
{
    return m_operations && m_operations->CanUndo();
}

bool Example::CanRedo() const
{
    return m_operations && m_operations->CanRedo();
}

void Example::InitializeScriptTree()
{
    m_scriptTreeView = TreeNode{};
    m_scriptTreeView.label = "Script";
    m_scriptTreeView.isOpen = true;
    m_scriptTreeView.icon = m_ScriptIcon;
    m_scriptTreeView.iconText = ICON_FA_FILE_CODE;
    m_scriptTreeView.id = m_script.ID;
    m_scriptTreeView.contextMenu = [this]()
    {
        if (ImGui::BeginPopupContextItem("SelectablePopup"))
        {
            if (ImGui::MenuItem("Add Function"))
                pendingActions.push_back(std::make_shared<AddFunctionAction>(this, m_IDGenerator.GetNextId()));
            if (ImGui::MenuItem("Add Variable"))
                pendingActions.push_back(std::make_shared<AddVariableAction>(this, m_IDGenerator.GetNextId()));
            if (ImGui::MenuItem("Add Class"))
            {
                const int id = m_IDGenerator.GetNextId();
                pendingActions.push_back(std::make_shared<DeferredAction>([this, id]()
                {
                    const std::string name = Utils::FindValidName("Class", m_scriptTreeView);
                    const OperationResult result = m_operations->AddClass(id, name);
                    m_fileStatusIsError = !result;
                    m_fileStatus = result ? "Class added" : result.error;
                    if (result) RebuildScriptTree();
                }));
            }
            ImGui::EndPopup();
        }
    };
}

void Example::RebuildScriptTree()
{
    std::set<int> openItems;
    std::stack<const TreeNode*> previousNodes;
    previousNodes.push(&m_scriptTreeView);
    while (!previousNodes.empty())
    {
        const TreeNode* current = previousNodes.top();
        previousNodes.pop();
        if (current->isOpen)
            openItems.insert(current->id);
        for (const TreeNode& child : current->children)
            previousNodes.push(&child);
    }

    InitializeScriptTree();

    if (m_script.main)
    {
        TreeNode mainNode;
        mainNode.id = m_script.main->ID;
        mainNode.label = m_script.main->functionDef->name;
        mainNode.icon = m_FunctionIcon;
        mainNode.iconText = ICON_FA_PLAY;
        mainNode.pElement = std::static_pointer_cast<IScriptElement>(m_script.main);
        mainNode.onclick = [this]() { ed::ClearSelection(); ChangeGraph(m_script.main); };
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

    for (const ScriptClassPtr& scriptClass : m_script.classes)
        m_scriptTreeView.AddChild(MakeClassNode(scriptClass));

    for (const ScriptPropertyPtr& variable : m_script.variables)
        m_scriptTreeView.AddChild(MakeVariableNode(variable->ID, variable->Name));

    std::stack<TreeNode*> rebuiltNodes;
    rebuiltNodes.push(&m_scriptTreeView);
    while (!rebuiltNodes.empty())
    {
        TreeNode* current = rebuiltNodes.top();
        rebuiltNodes.pop();
        if (current != &m_scriptTreeView)
            current->isOpen = openItems.count(current->id) != 0;
        for (TreeNode& child : current->children)
            rebuiltNodes.push(&child);
    }

    if (!FindNodeByID(m_selectedItemId))
        m_selectedItemId = m_script.main ? m_script.main->ID.id : m_script.ID.id;
    if (m_editingItemId > 0 && !FindNodeByID(m_editingItemId))
        m_editingItemId = -1;
}

TreeNode Example::MakeClassNode(const ScriptClassPtr& scriptClass)
{
    TreeNode node;
    node.id = scriptClass->ID.id;
    node.label = scriptClass->Name;
    node.icon = m_ClassIcon;
    node.iconText = ICON_FA_CUBES;
    node.pElement = std::static_pointer_cast<IScriptElement>(scriptClass);
    node.onRename = [this, id = scriptClass->ID.id](std::string name)
    {
        pendingActions.push_back(std::make_shared<DeferredAction>([this, id, name]()
        {
            const OperationResult result = m_operations->RenameClass(id, name);
            m_fileStatusIsError = !result;
            if (!result) m_fileStatus = result.error;
            else RebuildScriptTree();
        }));
    };
    node.contextMenu = [this, id = scriptClass->ID.id]()
    {
        if (!ImGui::BeginPopupContextItem("ClassPopup")) return;
        if (ImGui::MenuItem("Add Property"))
        {
            const int propertyId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id, propertyId]()
            {
                const OperationResult result = m_operations->AddClassProperty(id, propertyId, "Property");
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
        }
        if (ImGui::MenuItem("Add Method"))
        {
            const int methodId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id, methodId]()
            {
                const OperationResult result = m_operations->AddClassMethod(id, methodId, "Method");
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
        }
        ScriptClassPtr current = ScriptUtils::FindClassById(m_script, id);
        if (current && !current->constructor && ImGui::MenuItem("Add Constructor"))
        {
            const int constructorId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id, constructorId]()
            {
                const OperationResult result = m_operations->AddClassConstructor(id, constructorId);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
        }
        if (ImGui::MenuItem("Rename")) m_editingItemId = id;
        if (ImGui::MenuItem("Delete"))
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id]()
            {
                if (m_graphView.m_pScriptFunction && ScriptUtils::FindOwningClass(m_script,
                        m_graphView.m_pScriptFunction->ID.id) == ScriptUtils::FindClassById(m_script, id))
                    ChangeGraph(m_script.main);
                const OperationResult result = m_operations->RemoveClass(id);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
        ImGui::EndPopup();
    };

    if (scriptClass->constructor)
        node.AddChild(MakeConstructorNode(scriptClass->ID.id, scriptClass->constructor));
    for (const ScriptFunctionPtr& method : scriptClass->methods)
        node.AddChild(MakeClassMethodNode(scriptClass->ID.id, method));
    for (const ScriptPropertyPtr& property : scriptClass->properties)
        node.AddChild(MakeClassPropertyNode(scriptClass->ID.id, property));
    return node;
}

TreeNode Example::MakeClassMethodNode(int classId, const ScriptFunctionPtr& method)
{
    TreeNode node;
    node.id = method->ID.id;
    node.label = method->functionDef->name;
    node.icon = m_FunctionIcon;
    node.iconText = ICON_FA_DIAGRAM_PROJECT;
    node.pElement = std::static_pointer_cast<IScriptElement>(method);
    node.onclick = [this, method]() { ed::ClearSelection(); ChangeGraph(method); };
    node.onRename = [this, id = method->ID.id](std::string name)
    {
        pendingActions.push_back(std::make_shared<DeferredAction>([this, id, name]()
        {
            const OperationResult result = m_operations->RenameFunction(id, name);
            m_fileStatusIsError = !result;
            if (!result) m_fileStatus = result.error; else RebuildScriptTree();
        }));
    };
    node.contextMenu = [this, classId, id = method->ID.id]()
    {
        if (!ImGui::BeginPopupContextItem("MethodPopup")) return;
        if (ImGui::MenuItem("Add Input"))
            pendingActions.push_back(std::make_shared<AddFunctionInputAction>(this, id, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem("Add Output"))
            pendingActions.push_back(std::make_shared<AddFunctionOutputAction>(this, id, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem("Rename")) m_editingItemId = id;
        if (ImGui::MenuItem("Delete"))
            pendingActions.push_back(std::make_shared<DeferredAction>([this, classId, id]()
            {
                if (m_graphView.m_pScriptFunction && m_graphView.m_pScriptFunction->ID == id)
                    ChangeGraph(m_script.main);
                const OperationResult result = m_operations->RemoveClassMethod(classId, id);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
        ImGui::EndPopup();
    };
    for (const auto& input : method->functionDef->inputs)
        node.AddChild(MakeInputNode(method->ID.id, input.id, input.name));
    for (const auto& output : method->functionDef->outputs)
        node.AddChild(MakeOutputNode(method->ID.id, output.id, output.name));
    return node;
}

TreeNode Example::MakeConstructorNode(int classId, const ScriptFunctionPtr& constructor)
{
    TreeNode node;
    node.id = constructor->ID.id;
    node.label = "Constructor";
    node.icon = m_FunctionIcon;
    node.iconText = ICON_FA_WAND_MAGIC_SPARKLES;
    node.pElement = std::static_pointer_cast<IScriptElement>(constructor);
    node.onclick = [this, constructor]() { ed::ClearSelection(); ChangeGraph(constructor); };
    node.contextMenu = [this, classId, id = constructor->ID.id]()
    {
        if (!ImGui::BeginPopupContextItem("ConstructorPopup")) return;
        if (ImGui::MenuItem("Add Input"))
            pendingActions.push_back(std::make_shared<AddFunctionInputAction>(this, id, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem("Delete"))
            pendingActions.push_back(std::make_shared<DeferredAction>([this, classId, id]()
            {
                if (m_graphView.m_pScriptFunction && m_graphView.m_pScriptFunction->ID == id)
                    ChangeGraph(m_script.main);
                const OperationResult result = m_operations->RemoveClassConstructor(classId);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
        ImGui::EndPopup();
    };
    for (const auto& input : constructor->functionDef->inputs)
        node.AddChild(MakeInputNode(constructor->ID.id, input.id, input.name));
    return node;
}

TreeNode Example::MakeClassPropertyNode(int classId, const ScriptPropertyPtr& property)
{
    TreeNode node;
    node.id = property->ID.id;
    node.label = property->Name;
    node.icon = m_VariableIcon;
    node.iconText = ICON_FA_DATABASE;
    node.pElement = std::static_pointer_cast<IScriptElement>(property);
    node.onRename = [this, classId, id = property->ID.id](std::string name)
    {
        pendingActions.push_back(std::make_shared<DeferredAction>([this, classId, id, name]()
        {
            const OperationResult result = m_operations->RenameClassProperty(classId, id, name);
            m_fileStatusIsError = !result;
            if (!result) m_fileStatus = result.error; else RebuildScriptTree();
        }));
    };
    node.contextMenu = [this, classId, id = property->ID.id]()
    {
        ScriptPropertyPtr current = ScriptUtils::FindClassPropertyById(m_script, id);
        if (!current) return;
        if (ImGui::BeginPopupContextItem("ClassPropertyPopup"))
        {
            if (ImGui::MenuItem("Rename")) m_editingItemId = id;
            if (ImGui::MenuItem("Delete"))
                pendingActions.push_back(std::make_shared<DeferredAction>([this, classId, id]()
                {
                    const OperationResult result = m_operations->RemoveClassProperty(classId, id);
                    m_fileStatusIsError = !result;
                    if (!result) m_fileStatus = result.error; else RebuildScriptTree();
                }));
            ImGui::EndPopup();
        }

        ImGui::PushID(id);
        ImGui::SameLine();
        Value value = current->defaultValue;
        if (GraphViewUtils::DrawTypeInput(TypeOfValue(value), value))
            pendingActions.push_back(std::make_shared<DeferredAction>([this, classId, id, value]()
            {
                const OperationResult result = m_operations->ChangeClassPropertyValue(classId, id, value);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error;
            }));
        ImGui::SameLine();
        GraphViewUtils::DrawTypeSelection(current->defaultValue, [this, classId, id](PinType type)
        {
            Value value = MakeValueFromType(type);
            pendingActions.push_back(std::make_shared<DeferredAction>([this, classId, id, value]()
            {
                const OperationResult result = m_operations->ChangeClassPropertyValue(classId, id, value);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error;
            }));
        });
        ImGui::PopID();
    };
    return node;
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
    SetTitle(("Visual Lox - " + std::filesystem::path(path).filename().string()).c_str());
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
    m_commitPendingEdit = false;
    actionStack.clear();
    undoDepth = 0;
    m_operations->ResetHistory();
    m_constFoldingValues.clear();
    m_constFoldingIDs.clear();
    m_selectedItemId = m_script.main ? m_script.main->ID.id : 0;
    m_editingItemId = 0;
    RebuildScriptTree();
    m_graphView.SetGraph(&m_script, m_script.main, &m_script.main->Graph);

    m_currentScriptPath = path;
    m_fileStatus = "Opened " + std::filesystem::path(path).filename().string();
    SetTitle(("Visual Lox - " + std::filesystem::path(path).filename().string()).c_str());
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
