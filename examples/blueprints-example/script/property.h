# pragma once

#include "scriptElement.h"
#include "../graphs/typeSystem.h"

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
    std::string Description;
    TypeRef type = PinType::Any;
    Value defaultValue;
};

using ScriptPropertyPtr = std::shared_ptr<ScriptProperty>;
