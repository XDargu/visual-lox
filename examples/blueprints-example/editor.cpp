#include "editor.h"

#include "native/nodes/begin.h"
#include "native/nodes/commentBox.h"
#include "IconsFontAwesome6.h"


#include "utilities/utils.h"

#include <Natives.h>

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

struct ConsoleInputCancelled final : std::exception
{
};

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

std::string InspectorTypeName(const TypeRef& type)
{
    return type.ToString();
}

bool DrawInspectorValueEditor(const char* id, Value& value, bool allowTypeChange = true,
                              int depth = 0, const TypeRef* declaredType = nullptr)
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
        const bool minimumChanged = ImGui::InputDouble("##value", &minimum, 0.0, 0.0, "From %.15g");
        ImGui::PopID();
        ImGui::PushID("maximum");
        ImGui::SetNextItemWidth(-1.0f);
        const bool maximumChanged = ImGui::InputDouble("##value", &maximum, 0.0, 0.0, "To %.15g");
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
            list->append(declaredType && declaredType->kind == PinType::List
                ? MakeValueFromType(declaredType->ElementType()) : Value());
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
                const TypeRef* elementType =
                    declaredType && declaredType->kind == PinType::List
                    ? &declaredType->ElementType() : nullptr;
                if (DrawInspectorValueEditor(
                        "item", item,
                        !elementType || elementType->kind == PinType::Any,
                        depth + 1, elementType))
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

    class SynchronizedOutputBuffer final : public std::streambuf
    {
    public:
        SynchronizedOutputBuffer(std::mutex& mutex, std::string& output)
            : m_mutex(mutex), m_output(output)
        {
        }

    protected:
        int_type overflow(int_type character) override
        {
            if (traits_type::eq_int_type(character, traits_type::eof()))
                return traits_type::not_eof(character);

            std::lock_guard<std::mutex> lock(m_mutex);
            m_output.push_back(traits_type::to_char_type(character));
            return character;
        }

        std::streamsize xsputn(const char* text, std::streamsize length) override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_output.append(text, static_cast<size_t>(length));
            return length;
        }

    private:
        std::mutex& m_mutex;
        std::string& m_output;
    };

    struct CaptureSynchronizedStdout
    {
        CaptureSynchronizedStdout(std::mutex& mutex, std::string& output)
            : buffer(mutex, output), oldCoutStreamBuf(std::cout.rdbuf(&buffer)), oldCerrStreamBuf(std::cerr.rdbuf(&buffer))
        {
        }

        ~CaptureSynchronizedStdout()
        {
            Restore();
        }

        void Restore()
        {
            if (!oldCoutStreamBuf)
                return;

            std::cout.rdbuf(oldCoutStreamBuf);
            std::cerr.rdbuf(oldCerrStreamBuf);
            oldCoutStreamBuf = nullptr;
            oldCerrStreamBuf = nullptr;
        }

        SynchronizedOutputBuffer buffer;
        std::streambuf* oldCoutStreamBuf;
        std::streambuf* oldCerrStreamBuf;
    };
}

void Example::OnStart()
{
    LoadLayoutSettings();
    m_graphView.setIDGenerator(m_IDGenerator);
    m_graphView.Init(LargeNodeFont());
    m_graphView.setNodeRegistry(m_NodeRegistry);
    m_graphView.setNavigationHandlers(
        [this](int elementId) { m_pendingOriginId = elementId; },
        [this](const NodePtr& node) { m_pendingReferenceNode = node; });

    VM& vm = VM::getInstance();
    vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(m_NodeRegistry, vm);

        for (Value& value : m_constFoldingValues)
        {
            vm.markValue(value);
        }

        ScriptUtils::MarkScriptRoots(m_script);

        if (m_visualApplicationContext)
            m_visualApplicationContext->MarkRoots(vm);

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
    RegisterVisualApplicationLibrary(m_NodeRegistry);
    m_NodeRegistry.RegisterNatives(vm);
    setInputProvider([this]()
    {
        std::unique_lock<std::mutex> lock(m_consoleMutex);
        m_consoleWaitingForInput = true;
        m_focusConsoleInput = true;
        m_consoleInputReady.wait(lock, [this]() { return !m_consoleInputQueue.empty() || m_scriptExecutionCancelled; });
        m_consoleWaitingForInput = false;

        if (m_scriptExecutionCancelled)
            throw ConsoleInputCancelled();

        std::string input = std::move(m_consoleInputQueue.front());
        m_consoleInputQueue.pop_front();
        return input;
    });

    // Script ID
    m_script.ID = m_IDGenerator.GetNextId();

    // Add begin to main function
    m_script.main = std::make_shared<ScriptFunction>(m_IDGenerator.GetNextId(), "Main");
    EnsureMainSignature();

    // SetGraph is called during startup, before ImGui has a current window.
    // Keep the graph empty here so node registration is deferred to its first
    // rendered frame.
    m_graphView.SetGraph(&m_script, m_script.main, &m_script.main->Graph);

    NodePtr beginMain = BuildBeginNode(m_IDGenerator, m_script.main);
    NodeUtils::BuildNode(beginMain);
    m_script.main->Graph.AddNode(beginMain);
    // Application::Create renders one hidden frame before showing the maximized window, so place the node on the following frame when the final canvas size is available.
    m_graphView.FocusNodeOnNextFrame(static_cast<int>(beginMain->ID.Get()), 1.0f / 3.0f, 1.0f, 1);

    m_operations = std::make_unique<DocumentOperations>(m_script, m_IDGenerator, m_NodeRegistry);
    m_graphView.setDocumentOperations(*m_operations);

    RebuildScriptTree();
    ApplyEditorTheme();
    MarkDocumentSaved();
    m_recoveryAvailable = std::filesystem::exists(m_recoveryPath);
    if (m_recoveryAvailable)
        ShowToast("Recovery autosave available in File");
    RefreshWindowTitle();
}

void Example::OnStop()
{
    StopScriptExecution();
    clearInputProvider();
    StopVisualApplication();
    DestroyPendingVisualApplicationTextures();
    SaveLayoutSettings();
    m_graphView.Destroy();
    auto releaseTexture = [this](ImTextureID& id)
    {
        if (id)
        {
            DestroyTexture(id);
            id = nullptr;
        }
    };

    releaseTexture(m_HeaderBackground);

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
    nodeStyle.SelectedNodeBorderWidth = 4.0f;
    nodeStyle.LinkStrength = 90.0f;
    nodeStyle.ScrollDuration = 0.22f;
    nodeStyle.Colors[ed::StyleColor_Bg] = ImColor(24, 27, 34, 255);
    nodeStyle.Colors[ed::StyleColor_Grid] = ImColor(110, 122, 145, 20);
    nodeStyle.Colors[ed::StyleColor_NodeBg] = ImColor(31, 35, 44, 248);
    nodeStyle.Colors[ed::StyleColor_NodeBorder] = ImColor(85, 95, 115, 180);
    nodeStyle.Colors[ed::StyleColor_HovNodeBorder] = ImColor(93, 159, 245, 255);
    nodeStyle.Colors[ed::StyleColor_SelNodeBorder] = ImColor(255, 153, 0, 255);
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
            else if (key == "recent" && std::filesystem::exists(value))
                m_recentFiles.push_back(value);
        }
        catch (...)
        {
            // Ignore malformed user layout values and retain safe defaults.
        }
    }

    m_leftPaneWidth = ImClamp(m_leftPaneWidth, 220.0f, 480.0f);
    m_rightPaneWidth = ImMax(m_rightPaneWidth, 240.0f);
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
    for (const std::string& recent : m_recentFiles)
        file << "recent=" << recent << '\n';
}

void Example::ChangeGraph(const ScriptFunctionPtr& scriptFunction, bool recordHistory)
{
    if (!scriptFunction || scriptFunction == m_graphView.m_pScriptFunction)
        return;
    if (recordHistory && m_graphView.m_pScriptFunction)
    {
        m_graphBackHistory.push_back(m_graphView.m_pScriptFunction->ID.id);
        if (m_graphBackHistory.size() > 64)
            m_graphBackHistory.erase(m_graphBackHistory.begin());
        m_graphForwardHistory.clear();
    }
    m_graphView.SetGraph(&m_script, scriptFunction, &scriptFunction->Graph);
    ApplyEditorTheme();
}

void Example::NavigateGraphHistory(bool forward)
{
    std::vector<int>& source = forward ? m_graphForwardHistory : m_graphBackHistory;
    std::vector<int>& destination = forward ? m_graphBackHistory : m_graphForwardHistory;
    while (!source.empty())
    {
        const int targetId = source.back();
        source.pop_back();
        ScriptFunctionPtr target = m_script.main && m_script.main->ID.id == targetId
            ? m_script.main : ScriptUtils::FindFunctionById(m_script, targetId);
        if (!target)
            continue;
        if (m_graphView.m_pScriptFunction)
            destination.push_back(m_graphView.m_pScriptFunction->ID.id);
        ChangeGraph(target, false);
        return;
    }
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

    const int saveIconWidth = static_cast<int>(ImGui::GetFrameHeight());
    const int saveIconHeight = saveIconWidth;
    const int restoreIconWidth = saveIconWidth;
    const int restoreIconHeight = saveIconWidth;

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

        ImGui::SetCursorScreenPos(iconPanelPos);
# if IMGUI_VERSION_NUM < 18967
        ImGui::SetItemAllowOverlap();
# else
        ImGui::SetNextItemAllowOverlap();
# endif
        if (node->SavedState.empty())
        {
            if (ImGui::SmallButton(ICON_FA_FLOPPY_DISK "##save"))
                node->SavedState = node->State;
            Tooltip("Save this node's diagnostic state");
        }
        else
            ImGui::Dummy(ImVec2((float)saveIconWidth, (float)saveIconHeight));

        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
# if IMGUI_VERSION_NUM < 18967
        ImGui::SetItemAllowOverlap();
# else
        ImGui::SetNextItemAllowOverlap();
# endif
        if (!node->SavedState.empty())
        {
            if (ImGui::SmallButton(ICON_FA_CLOCK_ROTATE_LEFT "##restore"))
            {
                node->State = node->SavedState;
                ed::RestoreNodeState(node->ID);
                node->SavedState.clear();
            }
            Tooltip("Restore this node's diagnostic state");
        }
        else
            ImGui::Dummy(ImVec2((float)restoreIconWidth, (float)restoreIconHeight));

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
    {
        if (MonoFont()) ImGui::PushFont(MonoFont());
        ImGui::TextUnformatted(m_compileOutput.c_str());
        if (MonoFont()) ImGui::PopFont();
    }

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
                        if (result) RebuildScriptTree(propertyId);
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
                        if (result) RebuildScriptTree(methodId);
                    }));
            }
            const bool hasToString = std::any_of(
                selectedClass->methods.begin(), selectedClass->methods.end(),
                [](const ScriptFunctionPtr& method)
                {
                    return method && method->functionDef->name == "toString";
                });
            if (ImGui::MenuItem(
                    ICON_FA_WAND_MAGIC_SPARKLES "  toString Method",
                    nullptr, false, !hasToString))
            {
                const int classId = selectedClass->ID.id;
                const int methodId = m_IDGenerator.GetNextId();
                const int outputId = m_IDGenerator.GetNextId();
                pendingActions.push_back(std::make_shared<DeferredAction>(
                    [this, classId, methodId, outputId]()
                    {
                        const OperationResult result =
                            m_operations->AddClassToStringMethod(
                                classId, methodId, outputId);
                        m_fileStatusIsError = !result;
                        m_fileStatus =
                            result ? "toString method added" : result.error;
                        if (!result)
                            return;

                        RebuildScriptTree();
                        m_selectedItemId = methodId;
                        if (ScriptFunctionPtr method =
                                ScriptUtils::FindFunctionById(m_script, methodId))
                            ChangeGraph(method);
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
                        if (result) RebuildScriptTree(constructorId);
                    }));
            }
            ImGui::Separator();
        }

        const bool selectedMain =
            selectedFunction && m_script.main == selectedFunction;
        if (selectedFunction)
        {
            ImGui::TextDisabled("%s locals", selectedFunction->functionDef->name.c_str());
            if (ImGui::MenuItem(ICON_FA_DATABASE "  Local Variable"))
            {
                const int functionId = selectedFunction->ID.id;
                const int variableId = m_IDGenerator.GetNextId();
                pendingActions.push_back(std::make_shared<DeferredAction>(
                    [this, functionId, variableId]() { AddFunctionVariable(functionId, variableId); }));
            }
            ImGui::Separator();
        }
        if (selectedFunction && !selectedMain)
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
                if (result) RebuildScriptTree(id);
            }));
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    RenderTreeNode(m_scriptTreeView, m_selectedItemId, m_editingItemId,
                   m_scriptFilter.c_str(), &m_scrollToScriptItemId);
}

void Example::ShowInspector()
{
    PanelHeading(HeaderFont(), ICON_FA_SLIDERS, "Inspector");

    const auto queueOperation =
        [this](const char* successMessage, std::function<OperationResult()> operation,
               bool rebuildTree = false, int createdItemId = -1)
        {
            pendingActions.push_back(std::make_shared<DeferredAction>(
                [this, successMessage = std::string(successMessage),
                 operation = std::move(operation), rebuildTree, createdItemId]()
                {
                    const OperationResult result = operation();
                    m_fileStatusIsError = !result;
                    m_fileStatus = result ? successMessage : result.error;
                    if (result && rebuildTree)
                        RebuildScriptTree(createdItemId);
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
                ImGui::TextDisabled(node->Type == NodeType::CommentBox ? "COMMENT BOX" : "NODE");
                ImGui::PushFont(HeaderFont());
                ImGui::TextWrapped("%s", node->Name.c_str());
                ImGui::PopFont();
                ImGui::TextDisabled("Graph: %s",
                    m_graphView.m_pScriptFunction
                        ? m_graphView.m_pScriptFunction->functionDef->name.c_str()
                        : "Unknown");

                if (node->Type == NodeType::CommentBox && m_graphView.m_pScriptFunction)
                {
                    ImGui::Spacing();
                    ImGui::TextDisabled("LABEL");
                    std::string text = node->Name;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##comment-box-text", &text) && !text.empty())
                    {
                        const int functionId = m_graphView.m_pScriptFunction->ID.id;
                        const ed::NodeId nodeId = node->ID;
                        queueOperation("Comment box updated", [this, functionId, nodeId, text]()
                        {
                            return m_operations->ChangeCommentBoxText(functionId, nodeId, text);
                        });
                    }

                    ImGui::TextDisabled("COLOR");
                    CommentBoxNode* commentBox = static_cast<CommentBoxNode*>(node.get());
                    int selectedColor = static_cast<int>(commentBox->BoxColor);
                    const auto colorLabel = [](void*, int index, const char** output)
                    {
                        if (index < 0 || index >= CommentBoxColorCount)
                            return false;
                        *output = CommentBoxColorLabel(static_cast<CommentBoxColor>(index));
                        return true;
                    };
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo("##comment-box-color", &selectedColor, colorLabel, nullptr, CommentBoxColorCount))
                    {
                        const int functionId = m_graphView.m_pScriptFunction->ID.id;
                        const ed::NodeId nodeId = node->ID;
                        const CommentBoxColor color = static_cast<CommentBoxColor>(selectedColor);
                        queueOperation("Comment box color updated", [this, functionId, nodeId, color]()
                        {
                            return m_operations->ChangeCommentBoxColor(functionId, nodeId, color);
                        });
                    }
                }

                if (!node->GenericTypeProperties.empty() && m_graphView.m_pScriptFunction)
                {
                    ImGui::Spacing();
                    const int functionId = m_graphView.m_pScriptFunction->ID.id;
                    const ed::NodeId nodeId = node->ID;
                    for (const GenericTypeProperty& property : node->GenericTypeProperties)
                    {
                        ImGui::PushID(property.variableName.c_str());
                        const auto resolved = node->ResolvedTypeVariables.find(property.variableName);
                        const TypeRef currentType =
                            resolved != node->ResolvedTypeVariables.end()
                                ? resolved->second
                                : TypeRef(PinType::Any);
                        const std::string variableName = property.variableName;
                        GraphViewUtils::DrawDeclaredTypeSelection(
                            m_script, currentType,
                            [this, functionId, nodeId, variableName, &queueOperation](TypeRef type)
                            {
                                queueOperation("Node type updated",
                                    [this, functionId, nodeId, variableName, type]()
                                    {
                                        return m_operations->ChangeNodeTypeOverride(
                                            functionId, nodeId, variableName, type);
                                    });
                            }, property.label.c_str(), true);
                        ImGui::PopID();
                    }
                }

                if (node->Type != NodeType::CommentBox && ImGui::CollapsingHeader("Inputs", ImGuiTreeNodeFlags_DefaultOpen))
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
                        ImGui::TextDisabled("%s", InspectorTypeName(input.Type).c_str());

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
                                    "node-input", value, input.Type == PinType::Any,
                                    0, &input.Type))
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

                if (node->Type != NodeType::CommentBox && ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (node->Outputs.empty())
                        ImGui::TextDisabled("This node has no outputs.");
                    for (const Pin& output : node->Outputs)
                    {
                        ImGui::BulletText("%s", output.Name.empty()
                            ? "Output" : output.Name.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", InspectorTypeName(output.Type).c_str());
                    }
                }

                ImGui::Spacing();
                if (ImGui::Button(ICON_FA_CROSSHAIRS " Center on node", ImVec2(-1, 0)))
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

    if (selected->id == m_script.ID.id)
    {
        ImGui::TextDisabled("SCRIPT");
        ImGui::PushFont(HeaderFont());
        ImGui::TextUnformatted("Visual Lox Script");
        ImGui::PopFont();
        ImGui::Text("%zu function%s", m_script.functions.size(),
                    m_script.functions.size() == 1 ? "" : "s");
        ImGui::Text("%zu variable%s", m_script.variables.size(),
                    m_script.variables.size() == 1 ? "" : "s");
        ImGui::Text("%zu class%s", m_script.classes.size(),
                    m_script.classes.size() == 1 ? "" : "es");

        ImGui::Spacing();
        ImGui::TextDisabled("ADD SCRIPT ITEM");
        if (ImGui::Button(ICON_FA_DIAGRAM_PROJECT " Function", ImVec2(-1, 0)))
            pendingActions.push_back(std::make_shared<AddFunctionAction>(
                this, m_IDGenerator.GetNextId()));
        if (ImGui::Button(ICON_FA_DATABASE " Variable", ImVec2(-1, 0)))
            pendingActions.push_back(std::make_shared<AddVariableAction>(
                this, m_IDGenerator.GetNextId()));
        if (ImGui::Button(ICON_FA_CUBES " Class", ImVec2(-1, 0)))
        {
            const int classId = m_IDGenerator.GetNextId();
            queueOperation("Class added",
                [this, classId]()
                {
                    const std::string name =
                        Utils::FindValidName("Class", m_scriptTreeView);
                    return m_operations->AddClass(classId, name);
                }, true, classId);
        }
        return;
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
                }, true, propertyId);
        }
        if (ImGui::Button(ICON_FA_DIAGRAM_PROJECT " Method", ImVec2(-1, 0)))
        {
            const int classId = scriptClass->ID.id;
            const int methodId = m_IDGenerator.GetNextId();
            queueOperation("Class method added",
                [this, classId, methodId]()
                {
                    return m_operations->AddClassMethod(classId, methodId, "Method");
                }, true, methodId);
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
                }, true, constructorId);
        }
        ImGuiUtils::EndDisabled();
        if (scriptClass->constructor)
            ImGui::TextDisabled("This class already has a constructor.");

        const bool hasToString = std::any_of(
            scriptClass->methods.begin(), scriptClass->methods.end(),
            [](const ScriptFunctionPtr& method)
        {
            return method && method->functionDef->name == "toString";
        });
        ImGuiUtils::BeginDisabled(hasToString);
        if (ImGui::Button(ICON_FA_TEXT_WIDTH " To String", ImVec2(-1, 0)))
        {
            const int classId = scriptClass->ID.id;
            const int methodId = m_IDGenerator.GetNextId();
            const int outputId = m_IDGenerator.GetNextId();

            pendingActions.push_back(std::make_shared<DeferredAction>(
                [this, classId, methodId, outputId]()
            {
                const OperationResult result = m_operations->AddClassToStringMethod(classId, methodId, outputId);
                m_fileStatusIsError = !result;
                m_fileStatus = result ? "toString method added" : result.error;
                if (!result)
                    return;

                RebuildScriptTree();
                m_selectedItemId = methodId;
                if (ScriptFunctionPtr method = ScriptUtils::FindFunctionById(m_script, methodId))
                    ChangeGraph(method);
            }));
        }
        ImGuiUtils::EndDisabled();
        if (hasToString)
            ImGui::TextDisabled("This class already has a toString method");

        ImGui::Spacing();
        ImGui::Text("%zu propert%s", scriptClass->properties.size(),
                    scriptClass->properties.size() == 1 ? "y" : "ies");
        ImGui::Text("%zu method%s", scriptClass->methods.size(),
                    scriptClass->methods.size() == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_TRASH_CAN " Delete class", ImVec2(-1, 0)))
        {
            const int classId = scriptClass->ID.id;
            queueOperation("Class deleted",
                [this, classId]()
                {
                    const ScriptClassPtr current =
                        ScriptUtils::FindClassById(m_script, classId);
                    if (m_graphView.m_pScriptFunction && current &&
                        ScriptUtils::FindOwningClass(
                            m_script, m_graphView.m_pScriptFunction->ID.id) == current)
                        ChangeGraph(m_script.main);
                    return m_operations->RemoveClass(classId);
                }, true);
        }
        return;
    }

    if (property)
    {
        const ScriptClassPtr classOwner =
            ScriptUtils::FindOwningClass(m_script, property->ID.id);
        const ScriptFunctionPtr functionOwner = function &&
            ScriptUtils::FindFunctionVariableById(function, property->ID.id) ? function : nullptr;
        const bool isClassProperty = classOwner != nullptr;
        const bool isLocalVariable = functionOwner != nullptr;
        ImGui::TextDisabled(isClassProperty ? "CLASS PROPERTY" : isLocalVariable ? "LOCAL VARIABLE" : "GLOBAL VARIABLE");
        ImGui::PushFont(HeaderFont());
        ImGui::TextWrapped("%s", property->Name.c_str());
        ImGui::PopFont();
        if (classOwner)
            ImGui::TextDisabled("Class: %s", classOwner->Name.c_str());
        else if (functionOwner)
            ImGui::TextDisabled("Function: %s", functionOwner->functionDef->name.c_str());

        std::string name = property->Name;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("Name", &name))
        {
            const int propertyId = property->ID.id;
            if (classOwner)
            {
                const int classId = classOwner->ID.id;
                queueOperation("Property renamed",
                    [this, classId, propertyId, name]()
                    {
                        return m_operations->RenameClassProperty(
                            classId, propertyId, name);
                    }, true);
            }
            else if (functionOwner)
            {
                const int functionId = functionOwner->ID.id;
                queueOperation("Local variable renamed",
                    [this, functionId, propertyId, name]()
                    {
                        return m_operations->RenameFunctionVariable(functionId, propertyId, name);
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

        std::string description = property->Description;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextMultiline(
                "Description", &description, ImVec2(-1.0f, 72.0f)))
        {
            const int propertyId = property->ID.id;
            if (classOwner)
            {
                const int classId = classOwner->ID.id;
                queueOperation("Property description updated",
                    [this, classId, propertyId, description]()
                    {
                        return m_operations->ChangeClassPropertyDescription(
                            classId, propertyId, description);
                    }, true);
            }
            else if (functionOwner)
            {
                const int functionId = functionOwner->ID.id;
                queueOperation("Local variable description updated",
                    [this, functionId, propertyId, description]()
                    {
                        return m_operations->ChangeFunctionVariableDescription(functionId, propertyId, description);
                    }, true);
            }
            else
            {
                queueOperation("Variable description updated",
                    [this, propertyId, description]()
                    {
                        return m_operations->ChangeVariableDescription(
                            propertyId, description);
                    }, true);
            }
        }

        ImGui::Spacing();
        GraphViewUtils::DrawDeclaredTypeSelection(
            m_script, property->type,
            [this, classOwner, functionOwner, propertyId = property->ID.id, &queueOperation](TypeRef type)
            {
                if (classOwner)
                {
                    const int classId = classOwner->ID.id;
                    queueOperation("Property type updated",
                        [this, classId, propertyId, type]()
                        {
                            return m_operations->ChangeClassPropertyType(
                                classId, propertyId, type);
                        }, true);
                }
                else if (functionOwner)
                {
                    const int functionId = functionOwner->ID.id;
                    queueOperation("Local variable type updated",
                        [this, functionId, propertyId, type]()
                        {
                            return m_operations->ChangeFunctionVariableType(functionId, propertyId, type);
                        }, true);
                }
                else
                {
                    queueOperation("Variable type updated",
                        [this, propertyId, type]()
                        {
                            return m_operations->ChangeVariableType(propertyId, type);
                        }, true);
                }
            });
        ImGui::Spacing();
        ImGui::TextDisabled("DEFAULT VALUE");
        Value value = CloneInspectorValue(property->defaultValue);
        if (DrawInspectorValueEditor(
                "property-value", value, property->type == PinType::Any,
                0, &property->type))
        {
            const int propertyId = property->ID.id;
            if (classOwner)
            {
                const int classId = classOwner->ID.id;
                queueOperation("Property value updated",
                    [this, classId, propertyId, value]()
                    {
                        return m_operations->ChangeClassPropertyValue(
                            classId, propertyId, value);
                    });
            }
            else if (functionOwner)
            {
                const int functionId = functionOwner->ID.id;
                queueOperation("Local variable value updated",
                    [this, functionId, propertyId, value]()
                    {
                        return m_operations->ChangeFunctionVariableValue(functionId, propertyId, value);
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
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button(
                isClassProperty ? ICON_FA_TRASH_CAN " Delete property"
                                : ICON_FA_TRASH_CAN " Delete variable",
                ImVec2(-1, 0)))
        {
            const int propertyId = property->ID.id;
            if (classOwner)
            {
                const int classId = classOwner->ID.id;
                queueOperation("Property deleted",
                    [this, classId, propertyId]()
                    {
                        return m_operations->RemoveClassProperty(
                            classId, propertyId);
                    }, true);
            }
            else if (functionOwner)
            {
                const int functionId = functionOwner->ID.id;
                queueOperation("Local variable deleted",
                    [this, functionId, propertyId]()
                    {
                        return m_operations->RemoveFunctionVariable(functionId, propertyId);
                    }, true);
            }
            else
            {
                queueOperation("Variable deleted",
                    [this, propertyId]()
                    {
                        return m_operations->RemoveVariable(propertyId);
                    }, true);
            }
        }
        return;
    }

    if (function)
    {
        const ScriptClassPtr owner =
            ScriptUtils::FindOwningClass(m_script, function->ID.id);
        const bool isConstructor = owner && owner->constructor == function;
        const bool isMain = m_script.main == function;
        ImGui::TextDisabled(isMain ? "MAIN GRAPH"
                                  : isConstructor ? "CONSTRUCTOR"
                                                  : owner ? "METHOD" : "FUNCTION");
        ImGui::PushFont(HeaderFont());
        /*ImGui::TextWrapped("%s", isConstructor ? owner->Name.c_str()
                                                : function->functionDef->name.c_str());*/

        if (!isConstructor && !isMain)
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

        ImGui::PopFont();
        if (owner)
            ImGui::TextDisabled("Class: %s", owner->Name.c_str());

        ImGui::TextDisabled("DESCRIPTION");
        const int functionId = function->ID.id;
        std::string functionDescription = function->functionDef->description;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextMultiline("Description", &functionDescription, ImVec2(-1.0f, 72.0f)))
        {
            queueOperation("Function description updated",
                [this, functionId, functionDescription]()
                {
                    return m_operations->ChangeFunctionDescription(
                        functionId, functionDescription);
                }, true);
        }

        ImGui::Spacing();
        ImGui::Text("%zu local variable%s", function->variables.size(), function->variables.size() == 1 ? "" : "s");
        if (ImGui::Button(ICON_FA_DATABASE " Add local variable", ImVec2(-1, 0)))
        {
            const int variableId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>(
                [this, functionId, variableId]() { AddFunctionVariable(functionId, variableId); }));
        }

        if (isMain)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("PROGRAM INPUT");
            ImGui::TextUnformatted(ICON_FA_LIST "  Arguments");
            ImGui::SameLine();
            ImGui::TextDisabled("String List");
            ImGui::TextWrapped(
                "Main has a fixed Arguments input containing the command-line "
                "arguments passed to the program.");
            ImGui::Spacing();
            ImGui::TextDisabled("CURRENT LAUNCH");
            const std::vector<std::string>& arguments = GetArguments();
            if (arguments.empty())
                ImGui::TextDisabled("No program arguments.");
            else
                for (size_t i = 0; i < arguments.size(); ++i)
                    ImGui::BulletText("[%zu] %s", i, arguments[i].c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Main does not expose configurable outputs.");
            return;
        }

        if (!isConstructor)
        {
            bool pure = HasFlag(
                function->functionDef->flags, NodeDefinitionFlags::Pure);
            if (ImGui::Checkbox("Pure", &pure))
            {
                queueOperation(pure ? "Function marked pure"
                                    : "Function marked impure",
                    [this, functionId, pure]()
                    {
                        return m_operations->ChangeFunctionPurity(
                            functionId, pure);
                    }, true);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Pure functions cannot cause side effects or call impure "
                    "functions. Their graphs may use normal execution flow, but "
                    "calls to them appear as data-only nodes.");
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Inputs", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (function->functionDef->inputs.empty())
                ImGui::TextDisabled("No inputs.");

            for (size_t inputIndex = 0; inputIndex < function->functionDef->inputs.size(); ++inputIndex)
            {
                const BasicFunctionDef::Input& input = function->functionDef->inputs[inputIndex];
                ImGui::PushID(input.id);
                
                const std::string inputLabel = std::to_string(inputIndex + 1) + ". " + (input.name.empty() ? "Unnamed input" : input.name) + "  [" + InspectorTypeName(input.type) + "]###input";
                const bool inputExpanded = ImGui::TreeNodeEx(inputLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);

                if (!inputExpanded)
                {
                    ImGui::PopID();
                    continue;
                }

                ImGui::TextDisabled("NAME");
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

                GraphViewUtils::DrawDeclaredTypeSelection(
                    m_script, input.type,
                    [this, functionId, inputId = input.id, &queueOperation](TypeRef type)
                    {
                        queueOperation("Input type updated",
                            [this, functionId, inputId, type]()
                            {
                                return m_operations->ChangeFunctionInputType(
                                    functionId, inputId, type);
                            }, true);
                    }
                );

                ImGui::TextDisabled("DEFAULT VALUE");
                Value value = CloneInspectorValue(input.value);
                if (DrawInspectorValueEditor("input-default", value, input.type == PinType::Any, 0, &input.type))
                {
                    queueOperation("Input default updated",
                        [this, functionId, inputId = input.id, value]()
                        {
                            return m_operations->ChangeFunctionInputValue(
                                functionId, inputId, value);
                    });
                }

                std::string inputDescription = input.description;
                ImGui::TextDisabled("DESCRIPTION");
                ImGui::SetNextItemWidth(-1.0f);

                if (ImGui::InputTextMultiline( "##input-description", &inputDescription, ImVec2(-1.0f, 54.0f)))
                {
                    queueOperation("Input description updated",
                        [this, functionId, inputId = input.id,
                         inputDescription]()
                        {
                            return m_operations->ChangeFunctionInputDescription(
                                functionId, inputId, inputDescription);
                        }, true);
                }

                const char* removeInputLabel = ICON_FA_TRASH_CAN " Remove input";
                const float removeInputWidth = ImGui::CalcTextSize(removeInputLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - removeInputWidth));

                if (ImGui::SmallButton(removeInputLabel))
                {
                    queueOperation("Input removed",
                        [this, functionId, inputId = input.id]()
                        {
                            return m_operations->RemoveFunctionInput(
                                functionId, inputId);
                        }, true);
                }

                ImGui::TreePop();
                ImGui::Spacing();
                ImGui::PopID();
            }

            if (ImGui::Button(ICON_FA_PLUS " Add input", ImVec2(-1, 0)))
                pendingActions.push_back(std::make_shared<AddFunctionInputAction>(this, functionId, m_IDGenerator.GetNextId()));
        }

        if (!isConstructor && ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (function->functionDef->outputs.empty())
                ImGui::TextDisabled("No outputs.");

            for (size_t outputIndex = 0; outputIndex < function->functionDef->outputs.size(); ++outputIndex)
            {
                const BasicFunctionDef::Input& output = function->functionDef->outputs[outputIndex];
                ImGui::PushID(output.id);
                const std::string outputLabel = std::to_string(outputIndex + 1) + ". " + (output.name.empty() ? "Unnamed output" : output.name) + "  [" + InspectorTypeName(output.type) + "]###output";
                const bool outputExpanded = ImGui::TreeNodeEx(outputLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);

                if (!outputExpanded)
                {
                    ImGui::PopID();
                    continue;
                }

                ImGui::TextDisabled("NAME");
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

                GraphViewUtils::DrawDeclaredTypeSelection(
                    m_script, output.type,
                    [this, functionId, outputId = output.id, &queueOperation](TypeRef type)
                    {
                        queueOperation("Output type updated",
                            [this, functionId, outputId, type]()
                            {
                                return m_operations->ChangeFunctionOutputType(
                                    functionId, outputId, type);
                            }, true);
                    }
                );

                ImGui::TextDisabled("DEFAULT VALUE");
                Value value = CloneInspectorValue(output.value);

                if (DrawInspectorValueEditor("output-default", value, output.type == PinType::Any,0, &output.type))
                {
                    queueOperation("Output default updated",
                        [this, functionId, outputId = output.id, value]()
                        {
                            return m_operations->ChangeFunctionOutputValue(
                                functionId, outputId, value);
                    });
                }

                std::string outputDescription = output.description;
                ImGui::TextDisabled("DESCRIPTION");
                ImGui::SetNextItemWidth(-1.0f);

                if (ImGui::InputTextMultiline("##output-description", &outputDescription, ImVec2(-1.0f, 54.0f)))
                {
                    queueOperation("Output description updated",
                        [this, functionId, outputId = output.id,
                         outputDescription]()
                        {
                            return m_operations->ChangeFunctionOutputDescription(
                                functionId, outputId, outputDescription);
                        }, true);
                }

                const char* removeOutputLabel = ICON_FA_TRASH_CAN " Remove output";
                const float removeOutputWidth = ImGui::CalcTextSize(removeOutputLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - removeOutputWidth));

                if (ImGui::SmallButton(removeOutputLabel))
                {
                    queueOperation("Output removed",
                        [this, functionId, outputId = output.id]()
                        {
                            return m_operations->RemoveFunctionOutput(
                                functionId, outputId);
                        }, true);
                }

                ImGui::TreePop();
                ImGui::Spacing();
                ImGui::PopID();
            }

            if (ImGui::Button(ICON_FA_PLUS " Add output", ImVec2(-1, 0)))
                pendingActions.push_back(std::make_shared<AddFunctionOutputAction>(this, functionId, m_IDGenerator.GetNextId()));
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button(
                isConstructor ? ICON_FA_TRASH_CAN " Delete constructor"
                              : owner ? ICON_FA_TRASH_CAN " Delete method"
                                      : ICON_FA_TRASH_CAN " Delete function",
                ImVec2(-1, 0)))
        {
            if (isConstructor)
            {
                const int classId = owner->ID.id;
                queueOperation("Constructor deleted",
                    [this, classId, functionId]()
                    {
                        if (m_graphView.m_pScriptFunction &&
                            m_graphView.m_pScriptFunction->ID == functionId)
                            ChangeGraph(m_script.main);
                        return m_operations->RemoveClassConstructor(classId);
                    }, true);
            }
            else if (owner)
            {
                const int classId = owner->ID.id;
                queueOperation("Method deleted",
                    [this, classId, functionId]()
                    {
                        if (m_graphView.m_pScriptFunction &&
                            m_graphView.m_pScriptFunction->ID == functionId)
                            ChangeGraph(m_script.main);
                        return m_operations->RemoveClassMethod(
                            classId, functionId);
                    }, true);
            }
            else
            {
                queueOperation("Function deleted",
                    [this, functionId]()
                    {
                        if (m_graphView.m_pScriptFunction &&
                            m_graphView.m_pScriptFunction->ID == functionId)
                            ChangeGraph(m_script.main);
                        return m_operations->RemoveFunction(functionId);
                    }, true);
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

void Example::SelectScriptItem(int elementId)
{
    TreeNode* selected = FindNodeByID(elementId);
    if (!selected)
        return;

    m_showScriptExplorer = true;
    m_scriptFilter.clear();
    ed::ClearSelection();
    m_selectedItemId = elementId;
    m_scrollToScriptItemId = elementId;
    for (TreeNode* parent = selected->parentId >= 0
            ? FindNodeByID(selected->parentId) : nullptr;
         parent;
         parent = parent->parentId >= 0
            ? FindNodeByID(parent->parentId) : nullptr)
    {
        parent->isOpen = true;
    }
}

void Example::GoToOrigin(int elementId)
{
    SelectScriptItem(elementId);

    ScriptFunctionPtr function =
        m_script.main && m_script.main->ID.id == elementId
            ? m_script.main
            : ScriptUtils::FindFunctionById(m_script, elementId);
    if (function)
        ChangeGraph(function);
}

void Example::RunTextSearch()
{
    m_searchTitle = m_searchQuery.empty()
        ? "Search"
        : "Search for \"" + m_searchQuery + "\"";
    m_searchResults = ScriptSearch::Text(m_script, m_searchQuery);
    SetBottomPanel(BottomPanelTab::Search);
}

void Example::FindReferences(int referenceId, int definitionId)
{
    if (definitionId == ScriptElementID::Invalid)
        definitionId = referenceId;
    m_searchResults =
        ScriptSearch::References(m_script, referenceId, definitionId);
    const auto definition = std::find_if(
        m_searchResults.begin(), m_searchResults.end(),
        [](const ScriptSearchResult& result)
        {
            return result.detail == "Definition";
        });
    m_searchTitle = "References to " +
        (definition != m_searchResults.end()
            ? definition->label
            : std::string("#") + std::to_string(definitionId));
    SetBottomPanel(BottomPanelTab::Search);
}

void Example::FindReferences(const NodePtr& node)
{
    if (!node)
        return;

    if (node->refId.IsValid())
    {
        FindReferences(node->refId.id);
        return;
    }

    m_searchResults = ScriptSearch::References(m_script, *node);
    const std::string label = !node->DefinitionId.empty()
        ? node->DefinitionId
        : (!node->Name.empty() ? node->Name : node->SerializationType);

    m_searchTitle = "References to " + label;
    SetBottomPanel(BottomPanelTab::Search);
}

void Example::FocusSearchResult(const ScriptSearchResult& result)
{
    if (result.kind == ScriptSearchResultKind::GraphNode)
    {
        ScriptFunctionPtr function =
            m_script.main && m_script.main->ID.id == result.functionId
                ? m_script.main
                : ScriptUtils::FindFunctionById(m_script, result.functionId);
        if (!function || !function->Graph.FindNode(ed::NodeId(result.nodeId)))
        {
            ShowToast("That search result no longer exists");
            return;
        }

        SelectScriptItem(result.functionId);
        ChangeGraph(function);
        m_graphView.FocusNodeOnNextFrame(result.nodeId);
        return;
    }

    SelectScriptItem(result.elementId);
    if (result.kind == ScriptSearchResultKind::FunctionPort &&
        result.functionId != ScriptElementID::Invalid)
    {
        ScriptFunctionPtr function =
            m_script.main && m_script.main->ID.id == result.functionId
                ? m_script.main
                : ScriptUtils::FindFunctionById(m_script, result.functionId);
        if (function)
            ChangeGraph(function);
        return;
    }

    if (result.functionId != ScriptElementID::Invalid)
    {
        ScriptFunctionPtr owner = ScriptUtils::FindAnyFunctionById(m_script, result.functionId);
        if (owner)
            ChangeGraph(owner);
        return;
    }

    ScriptFunctionPtr function =
        m_script.main && m_script.main->ID.id == result.elementId
            ? m_script.main
            : ScriptUtils::FindFunctionById(m_script, result.elementId);
    if (function)
        ChangeGraph(function);
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

void Example::ShowSearchPanel()
{
    ImGui::TextUnformatted(m_searchTitle.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu result%s)", m_searchResults.size(),
                        m_searchResults.size() == 1 ? "" : "s");
    ImGui::SameLine(ImMax(
        ImGui::GetCursorPosX() + 12.0f,
        ImGui::GetWindowContentRegionMax().x - 70.0f));
    if (ImGui::SmallButton(ICON_FA_TRASH_CAN " Clear"))
    {
        m_searchQuery.clear();
        m_searchResults.clear();
        m_searchTitle = "Search";
    }
    ImGui::Separator();

    if (m_searchResults.empty())
    {
        ImGui::TextDisabled(
            m_searchQuery.empty()
                ? "Enter a term in the toolbar search box."
                : "No matching definitions, nodes, pins, or values.");
        return;
    }

    if (ImGui::BeginTable(
            "SearchResultsTable", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Matched", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "Location", ImGuiTableColumnFlags_WidthFixed, 210.0f);
        ImGui::TableHeadersRow();

        for (size_t index = 0; index < m_searchResults.size(); ++index)
        {
            const ScriptSearchResult& result = m_searchResults[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const char* icon =
                result.kind == ScriptSearchResultKind::GraphNode
                    ? ICON_FA_DIAGRAM_PROJECT
                    : result.kind == ScriptSearchResultKind::FunctionPort
                        ? ICON_FA_CIRCLE_DOT
                        : ICON_FA_FILE_CODE;
            const std::string rowLabel =
                std::string(icon) + "  " + result.label;
            if (ImGui::Selectable(
                    rowLabel.c_str(), false,
                    ImGuiSelectableFlags_SpanAllColumns))
                FocusSearchResult(result);
            Tooltip(result.kind == ScriptSearchResultKind::GraphNode
                ? "Open the graph, select this node, and center it"
                : "Select and reveal this definition in Script Explorer");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(result.detail.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", result.location.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void Example::ShowOutputPanel()
{
    if (ImGui::Button(ICON_FA_TRASH_CAN " Clear"))
    {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        m_runOutput.clear();
        m_consoleDisplay.clear();
    }

    const bool executionRunning = IsScriptExecutionRunning();
    ImGui::SameLine();
    if (executionRunning || m_visualApplicationContext)
    {
        if (ImGui::Button(ICON_FA_STOP " Stop"))
        {
            if (executionRunning)
                StopScriptExecution();
            else
                StopVisualApplication();
            m_fileStatus = "Program stopped";
            m_fileStatusIsError = false;
        }
    }
    else if (ImGui::Button(ICON_FA_PLAY " Run again"))
        CompileScript(true);

    ImGui::Separator();

    bool focusInput = false;
    bool waitingForInput = false;
    {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        if (m_consoleDisplay != m_runOutput)
            m_consoleDisplay = m_runOutput;
        waitingForInput = m_consoleWaitingForInput;
        focusInput = m_focusConsoleInput;
        m_focusConsoleInput = false;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.040f, 0.052f, 1.0f));
    ImGui::BeginChild("##console", ImVec2(0.0f, 0.0f), true);
    ImGui::PopStyleColor();

    if (MonoFont())
        ImGui::PushFont(MonoFont());

    const float inputHeight = waitingForInput ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y : 0.0f;
    const float transcriptHeight = ImMax(40.0f, ImGui::GetContentRegionAvail().y - inputHeight);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::InputTextMultiline("##console-transcript", &m_consoleDisplay, ImVec2(-1.0f, transcriptHeight), ImGuiInputTextFlags_ReadOnly);

    if (waitingForInput)
    {
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        if (focusInput)
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##console-input", &m_consoleInput, ImGuiInputTextFlags_EnterReturnsTrue))
            SubmitConsoleInput();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (MonoFont())
        ImGui::PopFont();
    ImGui::EndChild();
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
        const bool selectionPending = m_selectBottomPanelTab;
        const BottomPanelTab requestedTab = m_bottomPanelTab;

        char problemsLabel[96];
        snprintf(problemsLabel, sizeof(problemsLabel), ICON_FA_LIST_CHECK " Problems  %zu",
                 m_validationReport.diagnostics.size());
        ImGuiTabItemFlags problemFlags =
            selectionPending && requestedTab == BottomPanelTab::Problems
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(problemsLabel, nullptr, problemFlags))
        {
            if (!selectionPending || requestedTab == BottomPanelTab::Problems)
                m_bottomPanelTab = BottomPanelTab::Problems;
            if (selectionPending && requestedTab == BottomPanelTab::Problems)
                m_selectBottomPanelTab = false;
            ShowProblemsPanel();
            ImGui::EndTabItem();
        }

        char searchLabel[96];
        snprintf(searchLabel, sizeof(searchLabel),
                 ICON_FA_MAGNIFYING_GLASS " Search  %zu",
                 m_searchResults.size());
        ImGuiTabItemFlags searchFlags =
            selectionPending && requestedTab == BottomPanelTab::Search
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(searchLabel, nullptr, searchFlags))
        {
            m_bottomPanelTab = BottomPanelTab::Search;
            if (selectionPending && requestedTab == BottomPanelTab::Search)
                m_selectBottomPanelTab = false;
            ShowSearchPanel();
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags outputFlags =
            selectionPending && requestedTab == BottomPanelTab::Output
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(ICON_FA_TERMINAL " Output", nullptr, outputFlags))
        {
            if (!selectionPending || requestedTab == BottomPanelTab::Output)
                m_bottomPanelTab = BottomPanelTab::Output;
            if (selectionPending && requestedTab == BottomPanelTab::Output)
                m_selectBottomPanelTab = false;
            ShowOutputPanel();
            ImGui::EndTabItem();
        }

        if (m_showDeveloperTools)
        {
            ImGuiTabItemFlags developerFlags =
                selectionPending && requestedTab == BottomPanelTab::Developer
                    ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(ICON_FA_BUG " Developer", nullptr, developerFlags))
            {
                if (!selectionPending || requestedTab == BottomPanelTab::Developer)
                    m_bottomPanelTab = BottomPanelTab::Developer;
                if (selectionPending && requestedTab == BottomPanelTab::Developer)
                    m_selectBottomPanelTab = false;
                ShowDeveloperPanel();
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }
}

void Example::CompileScript(bool runAfterCompile)
{
    PollScriptExecution();
    if (IsScriptExecutionRunning())
    {
        m_fileStatus = IsScriptWaitingForInput() ? "Program is waiting for input" : "Program is already running";
        m_fileStatusIsError = false;
        SetBottomPanel(BottomPanelTab::Output);
        return;
    }

    StopVisualApplication();

    VM& vm = VM::getInstance();
    Utils::CaptureStdout captureCompilation;
    std::cout << "Compiling script...\n";

    ScriptCompileOptions compileOptions;
    compileOptions.enableConstantFolding = m_isConstFoldingEnabled;
    compileOptions.disassemble = m_showDeveloperTools;
    compileOptions.programArguments = GetArguments();
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

    m_visualApplicationContext = std::make_unique<VisualApplicationContext>(VisualApplicationTextureCallbacks{
        [this](const void* data, int width, int height) { return CreateTexture(data, width, height); },
        [this](ImTextureID texture) { m_visualApplicationTexturesPendingDestroy.push_back(texture); }
    });

    StartScriptExecution(compileResult.function);
    m_fileStatus = "Program running";
    m_fileStatusIsError = false;
    SetBottomPanel(BottomPanelTab::Output);
}

void Example::StartScriptExecution(ObjFunction* function)
{
    if (m_scriptExecutionThread.joinable())
        m_scriptExecutionThread.join();

    {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        m_runOutput.clear();
        m_consoleDisplay.clear();
        m_consoleInput.clear();
        m_consoleInputQueue.clear();
        m_scriptExecutionResult = InterpretResult::INTERPRET_OK;
        m_scriptExecutionRunning = true;
        m_scriptExecutionFinished = false;
        m_scriptExecutionCancelled = false;
        m_consoleWaitingForInput = false;
        m_focusConsoleInput = false;
    }

    m_scriptExecutionThread = std::thread([this, function]()
    {
        InterpretResult result = InterpretResult::INTERPRET_RUNTIME_ERROR;
        bool cancelled = false;

        {
            Utils::CaptureSynchronizedStdout captureExecution(m_consoleMutex, m_runOutput);
            try
            {
                result = ScriptRuntime::Execute(VM::getInstance(), function);
            }
            catch (const ConsoleInputCancelled&)
            {
                VM::getInstance().resetStack();
                cancelled = true;
            }
            catch (const std::exception& exception)
            {
                VM::getInstance().resetStack();
                std::cerr << "Program stopped: " << exception.what() << '\n';
            }
            catch (...)
            {
                VM::getInstance().resetStack();
                std::cerr << "Program stopped by an unexpected runtime error.\n";
            }
        }

        std::lock_guard<std::mutex> lock(m_consoleMutex);
        m_scriptExecutionResult = result;
        m_scriptExecutionCancelled = cancelled;
        m_scriptExecutionRunning = false;
        m_scriptExecutionFinished = true;
        m_consoleWaitingForInput = false;
    });
}

void Example::PollScriptExecution()
{
    InterpretResult result = InterpretResult::INTERPRET_OK;
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        if (!m_scriptExecutionFinished)
            return;

        result = m_scriptExecutionResult;
        cancelled = m_scriptExecutionCancelled;
        m_scriptExecutionFinished = false;
    }

    if (m_scriptExecutionThread.joinable())
        m_scriptExecutionThread.join();

    if (cancelled)
    {
        m_visualApplicationContext.reset();
        m_fileStatus = "Program stopped";
        m_fileStatusIsError = false;
        return;
    }

    if (result == InterpretResult::INTERPRET_OK)
    {
        if (m_visualApplicationContext && m_visualApplicationContext->HasUpdateFunction())
        {
            m_visualApplicationPreviewOpen = true;
            m_fileStatus = "Application running";
            m_fileStatusIsError = false;
            std::lock_guard<std::mutex> lock(m_consoleMutex);
            if (m_runOutput.empty())
                m_runOutput = "Application initialized. Close the preview or press Stop to end it.";
        }
        else
        {
            m_visualApplicationContext.reset();
            m_fileStatus = "Run completed";
            m_fileStatusIsError = false;
            std::lock_guard<std::mutex> lock(m_consoleMutex);
            if (m_runOutput.empty())
                m_runOutput = "Program completed with no output.";
        }
    }

    if (result != InterpretResult::INTERPRET_OK)
    {
        m_visualApplicationContext.reset();
        m_fileStatus = result == InterpretResult::INTERPRET_COMPILE_ERROR
            ? "Run stopped: compilation error"
            : "Run stopped: runtime error";
        m_fileStatusIsError = true;
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        if (m_runOutput.empty())
            m_runOutput = m_fileStatus;
    }
}

void Example::StopScriptExecution()
{
    {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        if (!m_scriptExecutionThread.joinable())
            return;
        m_scriptExecutionCancelled = true;
        m_consoleWaitingForInput = false;
    }
    m_consoleInputReady.notify_all();
    m_scriptExecutionThread.join();

    {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        m_scriptExecutionRunning = false;
        m_scriptExecutionFinished = false;
        m_consoleWaitingForInput = false;
        m_consoleInputQueue.clear();
    }
    m_visualApplicationContext.reset();
}

void Example::SubmitConsoleInput()
{
    {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        if (!m_consoleWaitingForInput)
            return;

        m_runOutput += "> ";
        m_runOutput += m_consoleInput;
        m_runOutput.push_back('\n');
        m_consoleInputQueue.push_back(m_consoleInput);
        m_consoleInput.clear();
        m_consoleWaitingForInput = false;
    }
    m_consoleInputReady.notify_one();
}

bool Example::IsScriptExecutionRunning() const
{
    std::lock_guard<std::mutex> lock(m_consoleMutex);
    return m_scriptExecutionRunning;
}

bool Example::IsScriptWaitingForInput() const
{
    std::lock_guard<std::mutex> lock(m_consoleMutex);
    return m_consoleWaitingForInput;
}

void Example::StopVisualApplication()
{
    m_visualApplicationPreviewOpen = false;
    m_visualApplicationContext.reset();
}

void Example::DestroyPendingVisualApplicationTextures()
{
    for (ImTextureID texture : m_visualApplicationTexturesPendingDestroy)
        DestroyTexture(texture);
    m_visualApplicationTexturesPendingDestroy.clear();
}

void Example::DrawVisualApplicationPreview(float deltaTime)
{
    if (!m_visualApplicationContext || IsScriptExecutionRunning())
        return;

    ImGui::SetNextWindowSize(ImVec2(640.0f, 720.0f), ImGuiCond_FirstUseEver);
    const bool visible = ImGui::Begin("Vlox Application Preview", &m_visualApplicationPreviewOpen);
    InterpretResult result = InterpretResult::INTERPRET_OK;
    std::string frameOutput;
    if (visible)
    {
        Utils::CaptureStdout captureExecution;
        m_visualApplicationContext->BeginFrame();
        result = ScriptRuntime::Call(VM::getInstance(), m_visualApplicationContext->GetUpdateFunction(), { Value(static_cast<double>(deltaTime)) });
        m_visualApplicationContext->EndFrame();
        frameOutput = captureExecution.Restore();
    }
    ImGui::End();

    if (!frameOutput.empty())
    {
        if (m_runOutput == "Application initialized. Close the preview or press Stop to end it.")
            m_runOutput.clear();
        m_runOutput += frameOutput;
    }

    if (!m_visualApplicationPreviewOpen)
    {
        StopVisualApplication();
        m_fileStatus = "Application stopped";
        m_fileStatusIsError = false;
    }
    else if (result != InterpretResult::INTERPRET_OK)
    {
        StopVisualApplication();
        m_fileStatus = "Application stopped: runtime error";
        m_fileStatusIsError = true;
        if (m_runOutput.empty())
            m_runOutput = m_fileStatus;
        SetBottomPanel(BottomPanelTab::Output);
    }
}

void Example::DrawMenuBar()
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem(ICON_FA_FILE_CIRCLE_PLUS "  New", "Ctrl+N"))
            RequestNew();
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open...", "Ctrl+O"))
            RequestOpenDialog();
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
        if (ImGui::BeginMenu(ICON_FA_CLOCK_ROTATE_LEFT "  Recent Files"))
        {
            if (m_recentFiles.empty())
                ImGui::MenuItem("No recent files", nullptr, false, false);
            for (const std::string& recent : m_recentFiles)
            {
                const std::string label = std::filesystem::path(recent).filename().string() +
                    "##" + recent;
                if (ImGui::MenuItem(label.c_str()))
                    RequestOpen(recent);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", recent.c_str());
            }
            ImGui::EndMenu();
        }
        if (m_recoveryAvailable &&
            ImGui::MenuItem(ICON_FA_CLOCK_ROTATE_LEFT "  Recover autosave"))
        {
            m_pendingDocumentAction = PendingDocumentAction::Recover;
            if (m_documentDirty)
                m_openUnsavedDialog = true;
            else
            {
                LoadScript(m_recoveryPath);
                m_currentScriptPath.clear();
                m_savedDocumentSnapshot.clear();
                m_documentDirty = true;
                RefreshWindowTitle();
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            RequestExit();
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
        ImGui::Separator();
        if (ImGui::MenuItem(
                ICON_FA_MAGNIFYING_GLASS "  Search", "Ctrl+F"))
            m_focusSearchBox = true;
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
        const bool executionRunning = IsScriptExecutionRunning();
        ImGuiUtils::BeginDisabled(executionRunning);
        if (ImGui::MenuItem(ICON_FA_CODE "  Compile", "Ctrl+Enter"))
            CompileScript(false);
        ImGuiUtils::EndDisabled();
        if (executionRunning || m_visualApplicationContext)
        {
            if (ImGui::MenuItem(ICON_FA_STOP "  Stop", "F5"))
            {
                if (executionRunning)
                    StopScriptExecution();
                else
                    StopVisualApplication();
                m_fileStatus = "Program stopped";
                m_fileStatusIsError = false;
            }
        }
        else if (ImGui::MenuItem(ICON_FA_PLAY "  Run", "F5"))
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
                ImGui::BulletText("The node palette supports fuzzy search, arrow keys, Enter, favorites, and recent nodes.");
                ImGui::BulletText("Drag from an output pin to a compatible input pin to connect nodes.");
                ImGui::BulletText("Main exposes a fixed Arguments string list containing the program arguments.");
                ImGui::BulletText("Changed scripts autosave to a recovery file every 20 seconds.");
            }

            if (ImGui::CollapsingHeader("Inspector", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::BulletText("Node: edit unconnected input values and inspect output types.");
                ImGui::BulletText("Variable or property: edit its name, type, and default value.");
                ImGui::BulletText("Function or method: edit its name, inputs, outputs, and defaults.");
                ImGui::BulletText("Lists and long strings use expanded editors that do not need to fit on a node.");
                ImGui::BulletText("Use the delete action in an item's Inspector or its Explorer context menu.");
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
                ImGui::BulletText("Search matches definitions, node and pin text, types, descriptions, and values.");
                ImGui::BulletText("Use Find References from node or Script Explorer context menus.");
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
                        { "F", "Center on selected nodes" },
                        { "Double-click node", "Open the referenced definition" },
                        { "Ctrl+F", "Focus whole-script search" },
                        { "Alt+Left / Alt+Right", "Move through graph history" },
                        { "Ctrl+N", "Create a new script" },
                        { "Ctrl+Enter", "Compile" },
                        { "F5", "Run" },
                        { "Ctrl+S", "Save" },
                        { "Ctrl+O", "Open" },
                        { "Ctrl+Z / Ctrl+Y", "Undo / redo" },
                        { "Ctrl+C / Ctrl+V", "Copy / paste" },
                        { "Delete", "Delete the selected graph item" },
                        { "F2", "Rename the selected script element" },
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
        RequestOpenDialog();
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
    Tooltip(CanUndo() ? "Undo (Ctrl+Z)" : "Nothing to undo");
    ImGui::SameLine();
    ImGuiUtils::BeginDisabled(!CanRedo());
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT "##redo"))
        RedoLastAction();
    ImGuiUtils::EndDisabled();
    Tooltip(CanRedo() ? "Redo (Ctrl+Y)" : "Nothing to redo");

    const float compileWidth =
        ImGui::CalcTextSize(ICON_FA_CODE " Compile").x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
    const bool executionRunning = IsScriptExecutionRunning();
    const bool programActive = executionRunning || m_visualApplicationContext;
    const char* runLabel = programActive ? ICON_FA_STOP " Stop" : ICON_FA_PLAY " Run";
    const float runWidth =
        ImGui::CalcTextSize(runLabel).x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
    const float actionsWidth = compileWidth + runWidth + ImGui::GetStyle().ItemSpacing.x;
    const float rightX = ImGui::GetWindowWidth() - actionsWidth -
                         ImGui::GetStyle().WindowPadding.x;

    const float searchButtonWidth =
        ImGui::CalcTextSize(ICON_FA_MAGNIFYING_GLASS).x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
    const float searchStart = ImGui::GetCursorPosX() +
                              ImGui::GetStyle().ItemSpacing.x;
    const float searchSpace = rightX - searchStart -
                              ImGui::GetStyle().ItemSpacing.x * 2.0f;
    if (searchSpace >= 160.0f)
    {
        const float searchWidth = ImClamp(
            searchSpace - searchButtonWidth -
                ImGui::GetStyle().ItemSpacing.x,
            120.0f, 340.0f);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImMax(
            searchStart,
            rightX - searchWidth - searchButtonWidth -
                ImGui::GetStyle().ItemSpacing.x * 2.0f));
        if (m_focusSearchBox)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusSearchBox = false;
        }
        ImGui::SetNextItemWidth(searchWidth);
        if (ImGui::InputTextWithHint(
                "##globalSearch",
                "Search nodes, pins, and values...",
                &m_searchQuery,
                ImGuiInputTextFlags_EnterReturnsTrue))
            RunTextSearch();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS "##runSearch"))
            RunTextSearch();
        Tooltip("Search the entire script (Enter or click, Ctrl+F to focus)");
    }

    if (rightX > ImGui::GetCursorPosX())
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(rightX);
    }
    ImGuiUtils::BeginDisabled(executionRunning);
    if (ImGui::Button(ICON_FA_CODE " Compile"))
        CompileScript(false);
    ImGuiUtils::EndDisabled();
    Tooltip("Compile the current script (Ctrl+Enter)");
    ImGui::SameLine();
    const ImVec4 runColor = programActive ? ImVec4(0.67f, 0.22f, 0.22f, 1.0f) : ImVec4(0.16f, 0.55f, 0.34f, 1.0f);
    const ImVec4 runHoverColor = programActive ? ImVec4(0.78f, 0.28f, 0.28f, 1.0f) : ImVec4(0.20f, 0.66f, 0.41f, 1.0f);
    const ImVec4 runActiveColor = programActive ? ImVec4(0.56f, 0.17f, 0.17f, 1.0f) : ImVec4(0.13f, 0.47f, 0.29f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, runColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, runHoverColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, runActiveColor);
    if (ImGui::Button(runLabel))
    {
        if (executionRunning)
        {
            StopScriptExecution();
            m_fileStatus = "Program stopped";
            m_fileStatusIsError = false;
        }
        else if (m_visualApplicationContext)
        {
            StopVisualApplication();
            m_fileStatus = "Application stopped";
            m_fileStatusIsError = false;
        }
        else
            CompileScript(true);
    }
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
    ImGui::TextDisabled("%s%s", documentName.c_str(), m_documentDirty ? "  *" : "");
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

void Example::HandleShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiID activeId = ImGui::GetActiveID();
    const bool editingText = GImGui && activeId != 0 &&
        GImGui->InputTextState.ID == activeId;
    const bool popupOpen = ImGui::IsPopupOpen(
        nullptr, ImGuiPopupFlags_AnyPopup);

    // Function keys are application commands and remain available regardless
    // of which editor panel owns keyboard focus.
    if (!popupOpen && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F5), false))
    {
        if (IsScriptExecutionRunning())
        {
            StopScriptExecution();
            m_fileStatus = "Program stopped";
            m_fileStatusIsError = false;
        }
        else if (m_visualApplicationContext)
        {
            StopVisualApplication();
            m_fileStatus = "Application stopped";
            m_fileStatusIsError = false;
        }
        else
            CompileScript(true);
    }
    if (!popupOpen && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F1), false))
        m_showHelp = true;

    // Text fields retain their native copy/paste and undo behavior. The old
    // implementation only checked whether InputTextState had ever been used,
    // which left almost every shortcut permanently disabled after one edit.
    if (editingText || popupOpen)
        return;

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F2), false) &&
        ed::GetSelectedObjectCount() == 0)
    {
        if (TreeNode* selected = FindNodeByID(m_selectedItemId);
            selected && selected->onRename)
        {
            m_editingItemId = selected->id;
        }
    }

    if (io.KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F), false))
            m_focusSearchBox = true;
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C), false))
            CopySelection();
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V), false))
            PasteClipboard();
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z), false))
        {
            if (io.KeyShift)
            {
                if (CanRedo()) RedoLastAction();
            }
            else if (CanUndo())
                UndoLastAction();
        }
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Y), false) && CanRedo())
            RedoLastAction();
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_S), false))
        {
            if (io.KeyShift)
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
            RequestOpenDialog();
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_N), false))
            RequestNew();
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false))
            CompileScript(false);
        return;
    }

    if (io.KeyAlt &&
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow), false))
        NavigateGraphHistory(false);
    if (io.KeyAlt &&
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow), false))
        NavigateGraphHistory(true);
    if (!io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home), false))
        ed::NavigateToContent();
    if (!io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F), false))
        ed::NavigateToSelection(false);
}

void Example::OnFrame(float deltaTime)
{
    PollScriptExecution();
    if (IsScriptWaitingForInput())
    {
        m_fileStatus = "Waiting for input";
        m_fileStatusIsError = false;
    }

    // Preview shutdown can happen after its image was submitted to ImGui. Keep
    // those textures alive until DX11 has rendered that frame, then release
    // them here before any draw commands for the next frame are created.
    DestroyPendingVisualApplicationTextures();

    // Pending actions
    for (auto& action : pendingActions)
    {
        action->Run();
    }

    pendingActions.clear();
    if (m_pendingOriginId != ScriptElementID::Invalid)
    {
        const int elementId = m_pendingOriginId;
        m_pendingOriginId = ScriptElementID::Invalid;
        GoToOrigin(elementId);
    }
    if (m_pendingReferenceNode)
    {
        NodePtr node = std::move(m_pendingReferenceNode);
        FindReferences(node);
    }
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
    HandleShortcuts();
    UpdateDocumentState(deltaTime);
    m_toastTime = ImMax(0.0f, m_toastTime - deltaTime);

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
    const float maxRightPaneWidth = ImMax(240.0f, workspaceWidth - minimumCanvasWidth -
        (m_showScriptExplorer ? m_leftPaneWidth : 0.0f) - splitterWidth);
    if (m_showScriptExplorer)
        m_leftPaneWidth = ImClamp(m_leftPaneWidth, 220.0f,
            ImMax(220.0f, workspaceWidth - minimumCanvasWidth -
                            (m_showInspector ? m_rightPaneWidth : 0.0f) - splitterWidth));
    if (m_showInspector)
        m_rightPaneWidth = ImClamp(m_rightPaneWidth, 240.0f, maxRightPaneWidth);

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
    ImGuiUtils::BeginDisabled(m_graphBackHistory.empty());
    if (ImGui::SmallButton(ICON_FA_ARROW_LEFT "##graphBack"))
        NavigateGraphHistory(false);
    ImGuiUtils::EndDisabled();
    Tooltip(m_graphBackHistory.empty() ? "No previous graph" : "Back to previous graph");
    ImGui::SameLine();
    ImGuiUtils::BeginDisabled(m_graphForwardHistory.empty());
    if (ImGui::SmallButton(ICON_FA_ARROW_RIGHT "##graphForward"))
        NavigateGraphHistory(true);
    ImGuiUtils::EndDisabled();
    Tooltip(m_graphForwardHistory.empty() ? "No next graph" : "Forward to next graph");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextUnformatted(ICON_FA_FILE_CODE " Script");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_FA_CHEVRON_RIGHT);

    ScriptClassPtr graphClass = m_graphView.m_pScriptFunction
        ? ScriptUtils::FindOwningClass(m_script, m_graphView.m_pScriptFunction->ID.id)
        : nullptr;
    if (graphClass)
    {
        ImGui::SameLine();
        ImGui::TextUnformatted(graphClass->Name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_FA_CHEVRON_RIGHT);
    }
    ImGui::SameLine();
    if (HeaderFont()) ImGui::PushFont(HeaderFont());
    ImGui::TextUnformatted(m_graphView.m_pScriptFunction
        ? m_graphView.m_pScriptFunction->functionDef->name.c_str() : "Graph");
    if (HeaderFont()) ImGui::PopFont();

    ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + 16.0f,
                          ImGui::GetWindowContentRegionMax().x - 260.0f));
    const bool canAutoLayout = m_graphView.CanAutoLayout();
    ImGuiUtils::BeginDisabled(!canAutoLayout);
    if (ImGui::SmallButton(ICON_FA_SITEMAP "##autoLayoutGraph"))
        m_graphView.RequestAutoLayout();
    ImGuiUtils::EndDisabled();
    Tooltip(canAutoLayout ? "Auto layout graph" : "Auto layout requires at least two nodes");
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_CROSSHAIRS "##centerOnSelection"))
        ed::NavigateToSelection(false);
    Tooltip("Center on selection (F)");
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_MAXIMIZE "##frameAllGraph"))
        ed::NavigateToContent();
    Tooltip("Frame all nodes (Home)");
    ImGui::SameLine();
    char zoomLabel[32];
    snprintf(zoomLabel, sizeof(zoomLabel), "%.0f%%##zoomReset", ed::GetCurrentZoom() * 100.0f);
    if (ImGui::SmallButton(zoomLabel))
        ed::NavigateToContent();
    Tooltip("Reset the view and frame all nodes");
    ImGui::Separator();

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
                             240.0f, maxRightPaneWidth, mainHeight, true);
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
    ShowDocumentDialogs();
    DrawVisualApplicationPreview(deltaTime);
    DrawToasts();
}

TreeNode Example::MakeFunctionNode(int funId, const std::string& name)
{
    TreeNode funcNode;
    funcNode.id = funId;
    funcNode.kind = TreeNodeKind::Function;
    funcNode.isDraggable = true;
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
        if (ImGui::MenuItem(
                ICON_FA_MAGNIFYING_GLASS "  Find References"))
            FindReferences(funId);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_DATABASE "  Add Local Variable"))
        {
            const int variableId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>(
                [this, funId, variableId]() { AddFunctionVariable(funId, variableId); }));
        }
        if (ImGui::MenuItem(ICON_FA_PLUS "  Add Input"))
            pendingActions.push_back(std::make_shared<AddFunctionInputAction>(
                this, funId, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem(ICON_FA_PLUS "  Add Output"))
            pendingActions.push_back(std::make_shared<AddFunctionOutputAction>(
                this, funId, m_IDGenerator.GetNextId()));
        ImGui::Separator();
        if (ImGui::MenuItem("Rename"))
            m_editingItemId = funId;
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
        {
            if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete"))
                pendingActions.push_back(
                    std::make_shared<DeleteFunctionAction>(this, pFun));
        }
    };

    if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
    {
        funcNode.pElement = std::static_pointer_cast<IScriptElement>(pFun);
    }

    return funcNode;
}

TreeNode Example::MakeVariableNode(int varId, const std::string& name, int ownerFunctionId)
{
    TreeNode varNode;
    varNode.kind = TreeNodeKind::Variable;
    varNode.isDraggable = true;
    varNode.label = name;
    varNode.icon = m_VariableIcon;
    varNode.iconText = ICON_FA_DATABASE;
    varNode.id = varId;
    varNode.dragOwnerId = ownerFunctionId;
    varNode.onclick = []() { ed::ClearSelection(); };
    varNode.onRename = [this, varId, ownerFunctionId](std::string newName)
    {
        pendingActions.push_back(std::make_shared<DeferredAction>([this, varId, ownerFunctionId, newName]()
        {
            const OperationResult result = ownerFunctionId >= 0
                ? m_operations->RenameFunctionVariable(ownerFunctionId, varId, newName)
                : m_operations->RenameVariable(varId, newName);
            m_fileStatusIsError = !result;
            m_fileStatus = result ? "Variable renamed" : result.error;
            if (result) RebuildScriptTree();
        }));
    };
    varNode.contextMenu = [this, varId, ownerFunctionId]()
    {
        ScriptPropertyPtr variable = ownerFunctionId >= 0
            ? ScriptUtils::FindFunctionVariableById(m_script, ownerFunctionId, varId)
            : ScriptUtils::FindVariableById(m_script, varId);
        if (variable)
        {
            if (ImGui::MenuItem(
                    ICON_FA_MAGNIFYING_GLASS "  Find References"))
                FindReferences(varId);
            ImGui::Separator();
            if (ImGui::MenuItem("Rename"))
                m_editingItemId = varId;
            if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete"))
                pendingActions.push_back(std::make_shared<DeferredAction>([this, varId, ownerFunctionId]()
                {
                    const OperationResult result = ownerFunctionId >= 0
                        ? m_operations->RemoveFunctionVariable(ownerFunctionId, varId)
                        : m_operations->RemoveVariable(varId);
                    m_fileStatusIsError = !result;
                    m_fileStatus = result ? "Variable deleted" : result.error;
                    if (result) RebuildScriptTree();
                }));
        }
    };
    varNode.afterLabel = [this, varId, ownerFunctionId]()
    {
        ScriptPropertyPtr variable = ownerFunctionId >= 0
            ? ScriptUtils::FindFunctionVariableById(m_script, ownerFunctionId, varId)
            : ScriptUtils::FindVariableById(m_script, varId);
        if (variable)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", variable->type.ToString().c_str());
        }
    };
    ScriptPropertyPtr variable = ownerFunctionId >= 0
        ? ScriptUtils::FindFunctionVariableById(m_script, ownerFunctionId, varId)
        : ScriptUtils::FindVariableById(m_script, varId);
    if (variable)
    {
        varNode.pElement = std::static_pointer_cast<IScriptElement>(variable);
    }

    return varNode;
}

TreeNode Example::MakeInputNode(int funId, int inputId, const std::string& name)
{
    TreeNode inputNode;
    inputNode.id = inputId;
    inputNode.kind = TreeNodeKind::Input;
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
                if (ImGui::MenuItem(
                        ICON_FA_MAGNIFYING_GLASS "  Find References"))
                    FindReferences(funId, inputId);
                ImGui::Separator();
                if (ImGui::MenuItem("Rename"))
                    m_editingItemId = inputId;
                if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete"))
                    pendingActions.push_back(
                        std::make_shared<DeleteFunctionInputAction>(
                            this, funId, inputId, pInput->name.c_str(), pInput->value));
            }
        }
    };
    inputNode.afterLabel = [this, funId, inputId]()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
        {
            if (BasicFunctionDef::Input* pInput =
                    pFun->functionDef->FindInputByID(inputId))
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", pInput->type.ToString().c_str());
            }
        }
    };

    return inputNode;
}

TreeNode Example::MakeOutputNode(int funId, int outputId, const std::string& name)
{
    TreeNode outputNode;
    outputNode.id = outputId;
    outputNode.kind = TreeNodeKind::Output;
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
                if (ImGui::MenuItem(
                        ICON_FA_MAGNIFYING_GLASS "  Find References"))
                    FindReferences(funId, outputId);
                ImGui::Separator();
                if (ImGui::MenuItem("Rename"))
                    m_editingItemId = outputId;
                if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete"))
                    pendingActions.push_back(
                        std::make_shared<DeleteFunctionOutputAction>(
                            this, funId, outputId, pOutput->name.c_str(), pOutput->value));
            }
        }
    };
    outputNode.afterLabel = [this, funId, outputId]()
    {
        if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(m_script, funId))
        {
            if (BasicFunctionDef::Input* pOutput =
                    pFun->functionDef->FindOutputByID(outputId))
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", pOutput->type.ToString().c_str());
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

void Example::AddFunction(int funId, bool beginRename)
{
    const std::string namestr = Utils::FindValidName("Func", m_scriptTreeView);
    const OperationResult result = m_operations->AddFunction(funId, namestr);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Function added" : result.error;
    if (result) RebuildScriptTree(beginRename ? funId : -1);
}

void Example::AddFunction(const ScriptFunctionPtr& pExistingFunction)
{
    if (pExistingFunction)
        AddFunction(pExistingFunction->ID, false);
}

void Example::AddVariable(int varId)
{
    const std::string namestr = Utils::FindValidName("Variable", m_scriptTreeView);
    const OperationResult result = m_operations->AddVariable(varId, namestr);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Variable added" : result.error;
    if (result) RebuildScriptTree(varId);
}

void Example::AddVariable(const ScriptPropertyPtr& pVariable)
{
    if (!pVariable) return;
    const OperationResult result = m_operations->AddVariable(pVariable->ID, pVariable->Name, pVariable->defaultValue);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Variable added" : result.error;
    if (result) RebuildScriptTree();
}

void Example::AddFunctionVariable(int functionId, int id)
{
    ScriptFunctionPtr function = ScriptUtils::FindAnyFunctionById(m_script, functionId);
    if (!function)
    {
        m_fileStatusIsError = true;
        m_fileStatus = "Function not found";
        return;
    }

    const std::string base = "Local Variable";
    std::string name = base;
    int suffix = 1;
    const auto nameExists = [&]
    {
        const bool localExists = std::any_of(function->variables.begin(), function->variables.end(),
            [&](const ScriptPropertyPtr& variable) { return variable && variable->Name == name; });
        const bool inputExists = std::any_of(function->functionDef->inputs.begin(), function->functionDef->inputs.end(),
            [&](const BasicFunctionDef::Input& input) { return input.name == name; });
        return localExists || inputExists;
    };
    while (nameExists())
        name = base + std::to_string(suffix++);

    const OperationResult result = m_operations->AddFunctionVariable(functionId, id, name);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Local variable added" : result.error;
    if (result) RebuildScriptTree(id);
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
    else RebuildScriptTree(inputId);
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
    else RebuildScriptTree(outputId);
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
        if (result) ShowToast("Copied");
        return;
    }

    const OperationResult result = m_operations->CopyScriptElement(m_selectedItemId);
    m_fileStatusIsError = !result;
    m_fileStatus = result ? "Copied script data" : result.error;
    if (result) ShowToast("Copied");
}

void Example::PasteClipboard()
{
    if (!m_operations->HasClipboard())
        return;

    if (m_operations->ClipboardContainsNodes())
    {
        if (!m_graphView.m_pScriptFunction) return;
        const int functionId = m_graphView.m_pScriptFunction->ID.id;
        const ImVec2 pasteAnchor = m_graphView.hasCanvasMousePosition
            ? m_graphView.lastCanvasMousePosition
            : ed::ScreenToCanvas(ImGui::GetMousePos());
        std::vector<int> pasted;
        const OperationResult result = m_operations->PasteNodes(
            functionId, pasted, std::make_pair(
                static_cast<double>(pasteAnchor.x),
                static_cast<double>(pasteAnchor.y)));
        m_fileStatusIsError = !result;
        m_fileStatus = result ? "Pasted nodes" : result.error;
        if (result)
        {
            ScriptFunctionPtr function = functionId == m_script.main->ID.id
                ? m_script.main : ScriptUtils::FindFunctionById(m_script, functionId);
            for (const int id : pasted)
            {
                NodePtr pastedNode = function->Graph.FindNode(ed::NodeId(id));
                if (pastedNode)
                    m_graphView.RegisterNode(pastedNode);
            }

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
    m_graphView.SetGraph(&m_script, function, &function->Graph, false);
    ApplyEditorTheme();
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
    m_graphView.SetGraph(&m_script, function, &function->Graph, false);
    ApplyEditorTheme();
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
    m_scriptTreeView.kind = TreeNodeKind::Script;
    m_scriptTreeView.icon = m_ScriptIcon;
    m_scriptTreeView.iconText = ICON_FA_FILE_CODE;
    m_scriptTreeView.id = m_script.ID;
    m_scriptTreeView.onclick = []() { ed::ClearSelection(); };
    m_scriptTreeView.contextMenu = [this]()
    {
        if (ImGui::MenuItem(ICON_FA_DIAGRAM_PROJECT "  Add Function"))
            pendingActions.push_back(std::make_shared<AddFunctionAction>(
                this, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem(ICON_FA_DATABASE "  Add Variable"))
            pendingActions.push_back(std::make_shared<AddVariableAction>(
                this, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem(ICON_FA_CUBES "  Add Class"))
        {
            const int id = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id]()
            {
                const std::string name = Utils::FindValidName("Class", m_scriptTreeView);
                const OperationResult result = m_operations->AddClass(id, name);
                m_fileStatusIsError = !result;
                m_fileStatus = result ? "Class added" : result.error;
                if (result) RebuildScriptTree(id);
            }));
        }
    };
}

void Example::EnsureMainSignature()
{
    if (!m_script.main)
        return;

    BasicFunctionDef& definition = *m_script.main->functionDef;
    const int argumentsId = definition.inputs.empty()
        ? m_IDGenerator.GetNextId() : definition.inputs.front().id;
    definition.inputs = {
        { "Arguments", Value(newList()), argumentsId,
          TypeRef::List(PinType::String) }
    };
    definition.outputs.clear();

    NodePtr begin = m_script.main->Graph.FindNodeIf(
        [](const NodePtr& node) { return node->Category == NodeCategory::Begin; });
    if (!begin)
        return;

    Pin* argumentsPin = nullptr;
    std::vector<ed::PinId> removedPins;
    for (Pin& output : begin->Outputs)
    {
        if (output.Type == PinType::Flow)
            continue;
        if (!argumentsPin)
            argumentsPin = &output;
        else
            removedPins.push_back(output.ID);
    }

    if (argumentsPin)
    {
        argumentsPin->Name = "Arguments";
        argumentsPin->Type = argumentsPin->DeclaredType =
            TypeRef::List(PinType::String);
    }
    else
    {
        begin->Outputs.emplace_back(
            m_IDGenerator.GetNextId(), "Arguments",
            TypeRef::List(PinType::String));
    }

    for (const ed::PinId pinId : removedPins)
    {
        std::vector<ed::LinkId> links;
        for (const Link& link : m_script.main->Graph.GetLinks())
            if (link.StartPinID == pinId || link.EndPinID == pinId)
                links.push_back(link.ID);
        for (const ed::LinkId linkId : links)
            m_script.main->Graph.DeleteLink(linkId);
    }
    stl::erase_if(begin->Outputs, [&](const Pin& output)
    {
        return std::find(removedPins.begin(), removedPins.end(), output.ID) !=
               removedPins.end();
    });
    NodeUtils::BuildNode(begin);
}

void Example::RebuildScriptTree(int createdItemId)
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
        mainNode.kind = TreeNodeKind::Function;
        mainNode.label = m_script.main->functionDef->name;
        mainNode.icon = m_FunctionIcon;
        mainNode.iconText = ICON_FA_PLAY;
        mainNode.pElement = std::static_pointer_cast<IScriptElement>(m_script.main);
        mainNode.onclick = [this]() { ed::ClearSelection(); ChangeGraph(m_script.main); };
        mainNode.contextMenu = [this]()
        {
            if (ImGui::MenuItem(
                    ICON_FA_MAGNIFYING_GLASS "  Find References"))
                FindReferences(m_script.main->ID.id);
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_DATABASE "  Add Local Variable"))
            {
                const int functionId = m_script.main->ID.id;
                const int variableId = m_IDGenerator.GetNextId();
                pendingActions.push_back(std::make_shared<DeferredAction>(
                    [this, functionId, variableId]() { AddFunctionVariable(functionId, variableId); }));
            }
        };
        if (!m_script.main->functionDef->inputs.empty())
        {
            TreeNode argumentsNode;
            argumentsNode.id = m_script.main->functionDef->inputs.front().id;
            argumentsNode.kind = TreeNodeKind::Input;
            argumentsNode.label = "Arguments  (String List)";
            argumentsNode.icon = m_InputIcon;
            argumentsNode.iconText = ICON_FA_LIST;
            argumentsNode.onclick = []() { ed::ClearSelection(); };
            const int argumentsId =
                m_script.main->functionDef->inputs.front().id;
            argumentsNode.contextMenu = [this, argumentsId]()
            {
                if (ImGui::MenuItem(
                        ICON_FA_MAGNIFYING_GLASS "  Find References"))
                    FindReferences(m_script.main->ID.id, argumentsId);
            };
            mainNode.AddChild(argumentsNode);
        }
        for (const ScriptPropertyPtr& variable : m_script.main->variables)
            mainNode.AddChild(MakeVariableNode(variable->ID.id, variable->Name, m_script.main->ID.id));
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
        for (const ScriptPropertyPtr& variable : function->variables)
            functionNode->AddChild(MakeVariableNode(variable->ID.id, variable->Name, function->ID.id));
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

    if (createdItemId >= 0)
    {
        if (TreeNode* created = FindNodeByID(createdItemId))
        {
            m_scriptFilter.clear();
            m_selectedItemId = createdItemId;
            m_editingItemId = created->onRename ? createdItemId : -1;
            for (TreeNode* parent = created->parentId >= 0
                    ? FindNodeByID(created->parentId) : nullptr;
                 parent;
                 parent = parent->parentId >= 0
                    ? FindNodeByID(parent->parentId) : nullptr)
            {
                parent->isOpen = true;
            }
        }
    }
}

TreeNode Example::MakeClassNode(const ScriptClassPtr& scriptClass)
{
    TreeNode node;
    node.id = scriptClass->ID.id;
    node.kind = TreeNodeKind::Class;
    node.isDraggable = true;
    node.label = scriptClass->Name;
    node.icon = m_ClassIcon;
    node.iconText = ICON_FA_CUBES;
    node.pElement = std::static_pointer_cast<IScriptElement>(scriptClass);
    node.onclick = []() { ed::ClearSelection(); };
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
        if (ImGui::MenuItem(
                ICON_FA_MAGNIFYING_GLASS "  Find References"))
            FindReferences(id);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_DATABASE "  Add Property"))
        {
            const int propertyId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id, propertyId]()
            {
                const OperationResult result = m_operations->AddClassProperty(id, propertyId, "Property");
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree(propertyId);
            }));
        }
        if (ImGui::MenuItem(ICON_FA_DIAGRAM_PROJECT "  Add Method"))
        {
            const int methodId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id, methodId]()
            {
                const OperationResult result = m_operations->AddClassMethod(id, methodId, "Method");
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree(methodId);
            }));
        }
        ScriptClassPtr current = ScriptUtils::FindClassById(m_script, id);
        if (current && !current->constructor &&
            ImGui::MenuItem(ICON_FA_WAND_MAGIC_SPARKLES "  Add Constructor"))
        {
            const int constructorId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id, constructorId]()
            {
                const OperationResult result = m_operations->AddClassConstructor(id, constructorId);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree(constructorId);
            }));
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename"))
            m_editingItemId = id;
        if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete"))
            pendingActions.push_back(std::make_shared<DeferredAction>([this, id]()
            {
                if (m_graphView.m_pScriptFunction && ScriptUtils::FindOwningClass(m_script,
                        m_graphView.m_pScriptFunction->ID.id) == ScriptUtils::FindClassById(m_script, id))
                    ChangeGraph(m_script.main);
                const OperationResult result = m_operations->RemoveClass(id);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
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
    node.kind = TreeNodeKind::ClassMethod;
    node.isDraggable = true;
    node.dragOwnerId = classId;
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
        if (ImGui::MenuItem(
                ICON_FA_MAGNIFYING_GLASS "  Find References"))
            FindReferences(id);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_DATABASE "  Add Local Variable"))
        {
            const int variableId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>(
                [this, id, variableId]() { AddFunctionVariable(id, variableId); }));
        }
        if (ImGui::MenuItem(ICON_FA_PLUS "  Add Input"))
            pendingActions.push_back(std::make_shared<AddFunctionInputAction>(this, id, m_IDGenerator.GetNextId()));
        if (ImGui::MenuItem(ICON_FA_PLUS "  Add Output"))
            pendingActions.push_back(std::make_shared<AddFunctionOutputAction>(this, id, m_IDGenerator.GetNextId()));
        ImGui::Separator();
        if (ImGui::MenuItem("Rename"))
            m_editingItemId = id;
        if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete"))
            pendingActions.push_back(std::make_shared<DeferredAction>([this, classId, id]()
            {
                if (m_graphView.m_pScriptFunction && m_graphView.m_pScriptFunction->ID == id)
                    ChangeGraph(m_script.main);
                const OperationResult result = m_operations->RemoveClassMethod(classId, id);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
    };
    for (const auto& input : method->functionDef->inputs)
        node.AddChild(MakeInputNode(method->ID.id, input.id, input.name));
    for (const auto& output : method->functionDef->outputs)
        node.AddChild(MakeOutputNode(method->ID.id, output.id, output.name));
    for (const ScriptPropertyPtr& variable : method->variables)
        node.AddChild(MakeVariableNode(variable->ID.id, variable->Name, method->ID.id));
    return node;
}

TreeNode Example::MakeConstructorNode(int classId, const ScriptFunctionPtr& constructor)
{
    TreeNode node;
    node.id = constructor->ID.id;
    node.kind = TreeNodeKind::Constructor;
    node.isDraggable = true;
    node.dragOwnerId = classId;
    node.label = "Constructor";
    node.icon = m_FunctionIcon;
    node.iconText = ICON_FA_WAND_MAGIC_SPARKLES;
    node.pElement = std::static_pointer_cast<IScriptElement>(constructor);
    node.onclick = [this, constructor]() { ed::ClearSelection(); ChangeGraph(constructor); };
    node.contextMenu = [this, classId, id = constructor->ID.id]()
    {
        if (ImGui::MenuItem(
                ICON_FA_MAGNIFYING_GLASS "  Find References"))
            FindReferences(classId, id);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_DATABASE "  Add Local Variable"))
        {
            const int variableId = m_IDGenerator.GetNextId();
            pendingActions.push_back(std::make_shared<DeferredAction>(
                [this, id, variableId]() { AddFunctionVariable(id, variableId); }));
        }
        if (ImGui::MenuItem(ICON_FA_PLUS "  Add Input"))
            pendingActions.push_back(std::make_shared<AddFunctionInputAction>(this, id, m_IDGenerator.GetNextId()));
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete"))
            pendingActions.push_back(std::make_shared<DeferredAction>([this, classId, id]()
            {
                if (m_graphView.m_pScriptFunction && m_graphView.m_pScriptFunction->ID == id)
                    ChangeGraph(m_script.main);
                const OperationResult result = m_operations->RemoveClassConstructor(classId);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
    };
    for (const auto& input : constructor->functionDef->inputs)
        node.AddChild(MakeInputNode(constructor->ID.id, input.id, input.name));
    for (const ScriptPropertyPtr& variable : constructor->variables)
        node.AddChild(MakeVariableNode(variable->ID.id, variable->Name, constructor->ID.id));
    return node;
}

TreeNode Example::MakeClassPropertyNode(int classId, const ScriptPropertyPtr& property)
{
    TreeNode node;
    node.id = property->ID.id;
    node.kind = TreeNodeKind::ClassProperty;
    node.isDraggable = true;
    node.dragOwnerId = classId;
    node.label = property->Name;
    node.icon = m_VariableIcon;
    node.iconText = ICON_FA_DATABASE;
    node.pElement = std::static_pointer_cast<IScriptElement>(property);
    node.onclick = []() { ed::ClearSelection(); };
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
        if (ImGui::MenuItem(
                ICON_FA_MAGNIFYING_GLASS "  Find References"))
            FindReferences(id);
        ImGui::Separator();
        if (ImGui::MenuItem("Rename"))
            m_editingItemId = id;
        if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete"))
            pendingActions.push_back(std::make_shared<DeferredAction>([this, classId, id]()
            {
                const OperationResult result =
                    m_operations->RemoveClassProperty(classId, id);
                m_fileStatusIsError = !result;
                if (!result) m_fileStatus = result.error; else RebuildScriptTree();
            }));
    };
    node.afterLabel = [this, classId, id = property->ID.id]()
    {
        ScriptPropertyPtr current = ScriptUtils::FindClassPropertyById(m_script, id);
        if (!current) return;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", current->type.ToString().c_str());
    };
    return node;
}

void Example::RefreshWindowTitle()
{
    const std::string documentName = m_currentScriptPath.empty()
        ? "Untitled.vlox"
        : std::filesystem::path(m_currentScriptPath).filename().string();
    SetTitle(("Visual Lox - " + documentName + (m_documentDirty ? " *" : "")).c_str());
}

void Example::MarkDocumentSaved()
{
    std::string snapshot;
    if (ScriptSerializer::SerializeToString(m_script, snapshot))
        m_savedDocumentSnapshot = std::move(snapshot);
    m_lastObservedRevision = m_operations ? m_operations->Revision() : 0;
    m_documentDirty = false;
    m_autosaveElapsed = 0.0f;
    RefreshWindowTitle();
}

void Example::UpdateDocumentState(float deltaTime)
{
    if (!m_operations)
        return;

    const std::uint64_t revision = m_operations->Revision();
    if (revision != m_lastObservedRevision)
    {
        m_lastObservedRevision = revision;
        std::string snapshot;
        if (ScriptSerializer::SerializeToString(m_script, snapshot))
        {
            const bool wasDirty = m_documentDirty;
            m_documentDirty = snapshot != m_savedDocumentSnapshot;
            if (wasDirty != m_documentDirty)
                RefreshWindowTitle();
        }
    }

    if (!m_documentDirty)
    {
        m_autosaveElapsed = 0.0f;
        return;
    }

    m_autosaveElapsed += deltaTime;
    if (m_autosaveElapsed >= 20.0f)
    {
        const SerializationResult recovery = ScriptSerializer::Save(m_script, m_recoveryPath);
        if (recovery)
        {
            m_recoveryAvailable = true;
            m_autosaveElapsed = 0.0f;
        }
        else
        {
            m_fileStatus = "Autosave failed: " + recovery.error;
            m_fileStatusIsError = true;
            m_autosaveElapsed = 0.0f;
        }
    }
}

void Example::ShowToast(const std::string& message)
{
    m_toastMessage = message;
    m_toastTime = 2.5f;
}

void Example::DrawToasts()
{
    if (m_toastTime <= 0.0f || m_toastMessage.empty())
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 18.0f,
               viewport->WorkPos.y + 62.0f),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.96f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##editorToast", nullptr, flags))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kSuccess);
        ImGui::TextUnformatted(ICON_FA_CIRCLE_CHECK);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(m_toastMessage.c_str());
    }
    ImGui::End();
}

void Example::AddRecentFile(const std::string& path)
{
    if (path.empty() || path == m_recoveryPath)
        return;
    const std::string normalized = std::filesystem::absolute(path).lexically_normal().string();
    m_recentFiles.erase(
        std::remove(m_recentFiles.begin(), m_recentFiles.end(), normalized),
        m_recentFiles.end());
    m_recentFiles.insert(m_recentFiles.begin(), normalized);
    if (m_recentFiles.size() > 8)
        m_recentFiles.resize(8);
}

void Example::NewScript()
{
    StopScriptExecution();
    StopVisualApplication();
    m_graphView.Destroy();
    m_script = Script();
    m_IDGenerator.Reset();
    m_script.ID = m_IDGenerator.GetNextId();
    m_script.main = std::make_shared<ScriptFunction>(m_IDGenerator.GetNextId(), "Main");
    EnsureMainSignature();
    NodePtr beginMain = BuildBeginNode(m_IDGenerator, m_script.main);
    NodeUtils::BuildNode(beginMain);
    m_script.main->Graph.AddNode(beginMain);

    m_operations = std::make_unique<DocumentOperations>(m_script, m_IDGenerator, m_NodeRegistry);
    m_graphView.setDocumentOperations(*m_operations);
    pendingActions.clear();
    actionStack.clear();
    undoDepth = 0;
    m_selectedItemId = m_script.main->ID.id;
    m_editingItemId = 0;
    m_graphBackHistory.clear();
    m_graphForwardHistory.clear();
    m_searchQuery.clear();
    m_searchResults.clear();
    m_searchTitle = "Search";
    RebuildScriptTree();
    m_graphView.SetGraph(&m_script, m_script.main, &m_script.main->Graph);
    ApplyEditorTheme();
    m_currentScriptPath.clear();
    m_fileStatus = "New script";
    m_fileStatusIsError = false;
    MarkDocumentSaved();
}

void Example::RequestOpen(const std::string& path)
{
    if (!m_documentDirty)
    {
        LoadScript(path);
        return;
    }
    m_pendingDocumentAction = PendingDocumentAction::Open;
    m_pendingDocumentPath = path;
    m_openUnsavedDialog = true;
}

void Example::RequestOpenDialog()
{
    if (m_documentDirty)
    {
        m_pendingDocumentAction = PendingDocumentAction::OpenDialog;
        m_pendingDocumentPath.clear();
        m_openUnsavedDialog = true;
        return;
    }

    if (const std::optional<std::string> path =
            SelectVloxFile(false, m_currentScriptPath))
        LoadScript(*path);
}

void Example::RequestNew()
{
    if (!m_documentDirty)
    {
        NewScript();
        return;
    }
    m_pendingDocumentAction = PendingDocumentAction::New;
    m_openUnsavedDialog = true;
}

void Example::RequestExit()
{
    if (!m_documentDirty)
    {
        m_allowClose = true;
        Quit();
        return;
    }
    m_pendingDocumentAction = PendingDocumentAction::Exit;
    m_openUnsavedDialog = true;
}

bool Example::CanClose()
{
    if (m_allowClose || !m_documentDirty)
        return true;
    m_pendingDocumentAction = PendingDocumentAction::Exit;
    m_openUnsavedDialog = true;
    return false;
}

void Example::ShowDocumentDialogs()
{
    if (m_openUnsavedDialog)
    {
        ImGui::OpenPopup("Unsaved changes");
        m_openUnsavedDialog = false;
    }

    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextUnformatted(ICON_FA_TRIANGLE_EXCLAMATION "  Save changes?");
    ImGui::PopStyleColor();
    ImGui::TextWrapped("Your current script has unsaved changes. Save them before continuing.");
    ImGui::Spacing();

    auto continuePendingAction = [this]()
    {
        const PendingDocumentAction action = m_pendingDocumentAction;
        const std::string path = m_pendingDocumentPath;
        m_pendingDocumentAction = PendingDocumentAction::None;
        m_pendingDocumentPath.clear();
        if (action == PendingDocumentAction::New)
            NewScript();
        else if (action == PendingDocumentAction::OpenDialog)
        {
            if (const std::optional<std::string> selected =
                    SelectVloxFile(false, m_currentScriptPath))
                LoadScript(*selected);
        }
        else if (action == PendingDocumentAction::Open)
            LoadScript(path);
        else if (action == PendingDocumentAction::Exit)
        {
            m_allowClose = true;
            Quit();
        }
        else if (action == PendingDocumentAction::Recover)
        {
            LoadScript(m_recoveryPath);
            m_currentScriptPath.clear();
            m_savedDocumentSnapshot.clear();
            m_documentDirty = true;
            RefreshWindowTitle();
        }
    };

    if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save", ImVec2(110.0f, 0)))
    {
        if (!m_currentScriptPath.empty())
            SaveScript(m_currentScriptPath);
        else if (const std::optional<std::string> path =
                     SelectVloxFile(true, "Untitled.vlox"))
            SaveScript(*path);
        if (!m_fileStatusIsError && !m_documentDirty)
        {
            ImGui::CloseCurrentPopup();
            continuePendingAction();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(110.0f, 0)))
    {
        ImGui::CloseCurrentPopup();
        continuePendingAction();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0)))
    {
        m_pendingDocumentAction = PendingDocumentAction::None;
        m_pendingDocumentPath.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
    m_fileStatusIsError = false;
    AddRecentFile(path);
    MarkDocumentSaved();
    std::error_code removeError;
    std::filesystem::remove(m_recoveryPath, removeError);
    m_recoveryAvailable = false;
    ShowToast("Saved");
}

void Example::LoadScript(const std::string& path)
{
    StopScriptExecution();
    StopVisualApplication();
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
    EnsureMainSignature();
    pendingActions.clear();
    m_commitPendingEdit = false;
    actionStack.clear();
    undoDepth = 0;
    m_operations->ResetHistory();
    m_constFoldingValues.clear();
    m_constFoldingIDs.clear();
    m_selectedItemId = m_script.main ? m_script.main->ID.id : 0;
    m_editingItemId = 0;
    m_searchQuery.clear();
    m_searchResults.clear();
    m_searchTitle = "Search";
    RebuildScriptTree();
    m_graphView.SetGraph(&m_script, m_script.main, &m_script.main->Graph);
    ApplyEditorTheme();

    m_currentScriptPath = path;
    m_fileStatus = "Opened " + std::filesystem::path(path).filename().string();
    m_fileStatusIsError = false;
    m_graphBackHistory.clear();
    m_graphForwardHistory.clear();
    AddRecentFile(path);
    MarkDocumentSaved();
}

void Example::ShowFileControls()
{
    if (ImGui::Button("Open"))
        RequestOpenDialog();
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
