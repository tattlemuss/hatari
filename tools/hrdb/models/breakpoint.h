#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include <string>
#include <vector>

struct Breakpoint
{
    void SetExpression(const std::string& exp);
    std::string     m_expression;
    uint32_t        m_id;
    uint32_t        m_pcHack;
    uint32_t        m_conditionCount;       // Condition count
    uint32_t        m_hitCount;      // hit count
    uint32_t        m_once;
    uint32_t        m_quiet;
    uint32_t        m_trace;
};

struct Breakpoints
{
    std::vector<Breakpoint> m_breakpoints;
};

#endif // BREAKPOINT_H
