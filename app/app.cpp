#include "app/app.h"
#include "devices/sequencer/sequencer_device.h"
#include "core/scheduler.h"

#include <thread>
#include <chrono>

namespace
{
    SequencerDevice g_device;
    Scheduler g_scheduler;
}

void App_Init()
{
    g_device.Init();
    g_scheduler.Init();
}

void App_Run()
{
    g_scheduler.Update();
    SchedulerFlags flags = g_scheduler.ConsumeFlags();

    if (flags.tick_1ms)
    {
        g_device.Tick1ms();
    }

    if (flags.tick_5ms)
    {
        g_device.Tick5ms();
    }

    if (flags.tick_10ms)
    {
        g_device.Tick10ms();
    }

    if (flags.tick_20ms)
    {
        g_device.Tick20ms();
    }

    g_device.Process();

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}