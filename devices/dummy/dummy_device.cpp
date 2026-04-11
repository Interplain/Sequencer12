#include "devices/dummy/dummy_device.h"
#include <iostream>

void DummyDevice::Init()
{
    tick_1ms_count_ = 0;
    tick_5ms_count_ = 0;
    tick_10ms_count_ = 0;
    tick_20ms_count_ = 0;

    state_ = false;
    report_pending_ = false;

    std::cout << "DummyDevice initialized" << std::endl;
}

void DummyDevice::Tick1ms()
{
    ++tick_1ms_count_;

    if ((tick_1ms_count_ % 1000) == 0)
    {
        state_ = !state_;
    }
}

void DummyDevice::Tick5ms()
{
    ++tick_5ms_count_;
}

void DummyDevice::Tick10ms()
{
    ++tick_10ms_count_;
}

void DummyDevice::Tick20ms()
{
    ++tick_20ms_count_;
    report_pending_ = true;
}

void DummyDevice::Process()
{
    if (!report_pending_)
    {
        return;
    }

    report_pending_ = false;

    static uint32_t last_reported_second = 0;
    uint32_t current_second = tick_1ms_count_ / 1000;

    if (current_second != last_reported_second)
    {
        last_reported_second = current_second;

        std::cout
            << "1ms=" << tick_1ms_count_
            << "  5ms=" << tick_5ms_count_
            << "  10ms=" << tick_10ms_count_
            << "  20ms=" << tick_20ms_count_
            << "  state=" << (state_ ? "ON" : "OFF")
            << std::endl;
    }
}