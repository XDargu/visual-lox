#pragma once

#include <iostream>
#include <stdexcept>

namespace Tests
{
inline void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class Runner
{
public:
    template <typename Function>
    void Group(const char* name, Function&& function)
    {
        std::cout << "\n[" << name << "]\n";
        function();
    }

    template <typename Function>
    void Test(const char* name, Function&& function)
    {
        ++m_TestCount;
        try
        {
            function();
            ++m_PassedCount;
            std::cout << "  PASS " << name << '\n';
        }
        catch (const std::exception& exception)
        {
            ++m_FailedCount;
            std::cerr << "  FAIL " << name << ": " << exception.what() << '\n';
        }
        catch (...)
        {
            ++m_FailedCount;
            std::cerr << "  FAIL " << name << ": unknown exception\n";
        }
    }

    int Finish() const
    {
        std::cout << "\n" << m_PassedCount << "/" << m_TestCount << " tests passed";
        if (m_FailedCount != 0)
            std::cout << " (" << m_FailedCount << " failed)";
        std::cout << ".\n";
        return m_FailedCount == 0 ? 0 : 1;
    }

private:
    int m_TestCount = 0;
    int m_PassedCount = 0;
    int m_FailedCount = 0;
};
}
