#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include <cstdint>
#include <string>
#include <vector>
#include "processor.h"

struct Breakpoint
{
    void SetExpression(const std::string& exp);
    std::string     m_expression;
    Processor       m_proc;             // 0 = CPU, 1 == DSP
    uint32_t        m_id;               // starts at 1 for each processor
    uint32_t        m_pcHack;
    uint32_t        m_conditionCount;   // Condition count
    uint32_t        m_hitCount;         // hit count
    uint32_t        m_once;
    uint32_t        m_quiet;
    uint32_t        m_trace;
};

struct Breakpoints
{
    std::vector<Breakpoint> m_breakpoints;
};

#endif // BREAKPOINT_H
