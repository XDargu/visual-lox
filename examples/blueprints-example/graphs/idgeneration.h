
# pragma once

struct IDGenerator
{
    int GetNextId()
    {
        return m_NextId++;
    }

    int m_NextId = 1;
};