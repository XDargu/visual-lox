# pragma once

#include "scriptElement.h"

#include <Value.h>

#include <string>
#include <vector>
#include <memory>

struct ScriptProperty : public IScriptElement
{
    ScriptProperty(ScriptElementID id, const char* name)
        : Name(name)
    {
        ID = id;
        Type = ScriptElementType::Variable;
    }

    std::string Name;
    Value defaultValue;
};

using ScriptPropertyPtr = std::shared_ptr<ScriptProperty>;