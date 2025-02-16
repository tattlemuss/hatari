#ifndef PROCESSOR_H
#define PROCESSOR_H

// Identifies processor
// WARNING: this is serialized in settings
enum Processor
{
    kProcCpu = 0,
    kProcDsp = 1,
    kProcCount = 2
};

#endif // PROCESSOR_H
