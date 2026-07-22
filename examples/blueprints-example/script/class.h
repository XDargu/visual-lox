# pragma once

#include "scriptElement.h"
#include "function.h"
#include "property.h"

#include <string>
#include <vector>
#include <memory>

struct ScriptClass : public IScriptElement
{
    ScriptClass(ScriptElementID id = ScriptElementID::Invalid, const char* name = "Class")
        : Name(name)
    {
        ID = id;
        Type = ScriptElementType::Class;
    }

    std::string Name;

    std::vector<ScriptPropertyPtr> properties;
    std::vector<ScriptFunctionPtr> methods;

    // Constructors are stored separately because they have initializer return
    // semantics and are exposed as a class call rather than a normal method.
    ScriptFunctionPtr constructor = nullptr;
};

using ScriptClassPtr = std::shared_ptr<ScriptClass>;
