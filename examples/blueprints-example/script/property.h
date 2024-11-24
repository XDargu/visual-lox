# pragma once

#include <Value.h>

#include <string>
#include <vector>
#include <memory>

struct ScriptProperty
{
    int Id = -1;
    std::string Name;
    Value defaultValue;
};