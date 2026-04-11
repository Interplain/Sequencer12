#include "core/scheduler.h"

void Scheduler::Init()
{
    tick_count_ = 0;
    flags_ = {};
}

void Scheduler::Update()
{
    ++tick_count_;

    flags_.tick_1ms = true;

    if ((tick_count_ % 5) == 0)
    {
        flags_.tick_5ms = true;
    }

    if ((tick_count_ % 10) == 0)
    {
        flags_.tick_10ms = true;
    }

    if ((tick_count_ % 20) == 0)
    {
        flags_.tick_20ms = true;
    }
}

SchedulerFlags Scheduler::ConsumeFlags()
{
    SchedulerFlags out = flags_;
    flags_ = {};
    return out;
}