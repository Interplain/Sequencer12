#pragma once

class Device
{
public:
    virtual ~Device() = default;

    virtual void Init() = 0;

    virtual void Tick1ms() = 0;
    virtual void Tick5ms() = 0;
    virtual void Tick10ms() = 0;
    virtual void Tick20ms() = 0;

    virtual void Process() = 0;
};