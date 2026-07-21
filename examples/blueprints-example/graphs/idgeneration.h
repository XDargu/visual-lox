
# pragma once

struct IDGenerator
{
    int GetNextId()
    {
        return m_NextId++;
    }

    void Reset(int nextId = 1) { m_NextId = nextId; }
    int PeekNextId() const { return m_NextId; }

    int m_NextId = 1;
};
