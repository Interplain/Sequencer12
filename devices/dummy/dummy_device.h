#pragma once

#include "core/device.h"
#include <cstdint>

class DummyDevice : public Device
{
public:
    void Init() override;

    void Tick1ms() override;
    void Tick5ms() override;
    void Tick10ms() override;
    void Tick20ms() override;

    void Process() override;

private:
    uint32_t tick_1ms_count_ = 0;
    uint32_t tick_5ms_count_ = 0;
    uint32_t tick_10ms_count_ = 0;
    uint32_t tick_20ms_count_ = 0;

    bool state_ = false;
    bool report_pending_ = false;
};