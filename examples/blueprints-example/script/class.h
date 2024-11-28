# pragma once

#include "scriptElement.h"
#include "function.h"
#include "property.h"

#include <string>
#include <vector>
#include <memory>

struct ScriptClass : public IScriptElement
{
    ScriptClass()
    {
        Type = ScriptElementType::Class;
        constructor = std::make_shared<ScriptFunction>();
    }

    std::string Name;

    std::vector<ScriptPropertyPtr> properties;
    std::vector<ScriptFunctionPtr> methods;

    ScriptFunctionPtr constructor; // TODO: Should the constructor just be a method? We could just store the ID.
};

using ScriptClassPtr = std::shared_ptr<ScriptClass>;