
# pragma once

#include "uuid.h"

#include <imgui_node_editor.h>

#include <map>
#include <string>

namespace ed = ax::NodeEditor;

struct Link
{
    ed::LinkId ID;
    PersistentLinkId PersistentId{ Uuid::NewV4() };

    ed::PinId StartPinID;
    ed::PinId EndPinID;

    ImColor Color;
    bool IsResolved = true;
    std::map<std::string, std::string> SerializedExtensions;

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId) :
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255)
    {
    }
};
