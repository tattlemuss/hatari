#ifndef HISTORY_H
#define HISTORY_H

struct HistoryEntry
{
    uint32_t is_dsp;
    uint32_t pc;
};

struct History
{
    QVector<HistoryEntry> entries;
};
#endif // HISTORY_H
