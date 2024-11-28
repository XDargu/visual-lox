# pragma once

#include "scriptElement.h"

#include <Value.h>

#include <string>
#include <vector>
#include <memory>

struct ScriptProperty : public IScriptElement
{
    ScriptProperty()
    {
        Type = ScriptElementType::Variable;
    }

    std::string Name;
    Value defaultValue;
};

using ScriptPropertyPtr = std::shared_ptr<ScriptProperty>;