# pragma once

#include "class.h"
#include "function.h"
#include "property.h"

#include <string>
#include <vector>
#include <memory>

using ScriptElementID = int;

struct IScriptElement
{
    ScriptElementID ID;
};