#pragma once

#include <vector>
#include <string_view>
#include <string>
#include <algorithm>

namespace Utils
{
    std::vector<std::string> split(const std::string_view s, const std::string_view delimiter);
    std::string to_lower(const std::string_view s);
    std::string to_upper(const std::string_view s);
}

// TODO: Move to new file
namespace stl
{
    template<class Container, class Func>
    inline void erase_if(Container& c, Func func)
    {
        c.erase(
            std::remove_if(c.begin(), c.end(), func)
            , c.end()
        );
    }
}