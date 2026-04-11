#pragma once

#include <cstdint>

struct SchedulerFlags
{
    bool tick_1ms = false;
    bool tick_5ms = false;
    bool tick_10ms = false;
    bool tick_20ms = false;
};

class Scheduler
{
public:
    void Init();
    void Update();
    SchedulerFlags ConsumeFlags();

private:
    uint32_t tick_count_ = 0;
    SchedulerFlags flags_;
};