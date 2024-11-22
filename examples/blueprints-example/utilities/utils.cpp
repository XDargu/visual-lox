# include "utils.h"

namespace Utils
{
    std::vector<std::string> split(const std::string_view s, const std::string_view delimiter)
    {
        if (delimiter.empty()) return { std::string(s) };

        std::vector<std::string> tokens;
        size_t pos = 0;
        std::string_view leftover(s);

        while ((pos = leftover.find(delimiter)) != std::string::npos)
        {
            tokens.push_back(std::string(leftover.substr(0, pos)));
            leftover = leftover.substr(pos + delimiter.length());
        }
        tokens.push_back(std::string(leftover));

        return tokens;
    }

    std::string to_lower(const std::string_view s)
    {
        std::string lower;
        lower.reserve(s.length());

        for (char c : s)
        {
            lower += std::tolower(c);
        }

        return lower;
    }
}