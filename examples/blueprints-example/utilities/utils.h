#pragma once

#include <vector>
#include <string_view>
#include <string>

namespace Utils
{
    std::vector<std::string> split(const std::string_view s, const std::string_view delimiter);
}