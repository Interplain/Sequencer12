#pragma once
#include <cstdint>
#include "platform/midi/midi_parser.h"

namespace midi
{

// ─────────────────────────────────────────────
// MIDI clock operates at 24 pulses per quarter note (24ppqn)
// ─────────────────────────────────────────────

constexpr uint8_t kPPQN = 24;

enum class ClockMode : uint8_t
{
    Master = 0,   // S12 generates clock
    Slave         // S12 follows external clock
};

enum class ClockState : uint8_t
{
    Stopped = 0,
    Playing,
    Continuing
};

// ─────────────────────────────────────────────
// Callback interface
// Host (SequencerDevice) implements these
// ─────────────────────────────────────────────

class MidiClockListener
{
public:
    virtual ~MidiClockListener() = default;
    virtual void OnClockTick()     = 0;   // called every 24ppqn tick
    virtual void OnClockStart()    = 0;
    virtual void OnClockStop()     = 0;
    virtual void OnClockContinue() = 0;
};

// ─────────────────────────────────────────────
// MidiClock
// ─────────────────────────────────────────────

class MidiClock
{
public:
    void Init(ClockMode mode, MidiClockListener* listener);

    // ── Master mode ───────────────────────────────────────────────────────────
    void SetBpm(uint32_t bpm);
    uint32_t GetBpm() const;

    void Start();
    void Stop();
    void Continue();

    // Call every 1ms from Tick1ms()
    void Tick1ms();

    // ── Slave mode ────────────────────────────────────────────────────────────
    // Feed incoming UART bytes here
    void ReceiveByte(uint8_t byte);

    // ── Common ────────────────────────────────────────────────────────────────
    void        SetMode(ClockMode mode);
    ClockMode   GetMode()  const;
    ClockState  GetState() const;
    uint32_t    GetSlaveBpm() const;   // estimated BPM from incoming clock

    // ── Master output stub ────────────────────────────────────────────────────
    // On hardware this calls HAL_UART_Transmit
    // In sim this prints to std::cout
    void SendByte(uint8_t byte);

private:
    void SendClock();
    void SendStart();
    void SendStop();
    void SendContinue();
    void UpdateSlaveBpm(uint32_t tick_interval_ms);

    ClockMode         mode_           = ClockMode::Master;
    ClockState        state_          = ClockState::Stopped;
    MidiClockListener* listener_      = nullptr;
    MidiParser        parser_{};

    // Master
    uint32_t    bpm_              = 120;
    uint32_t    tick_interval_ms_ = 0;   // ms between 24ppqn ticks
    uint32_t    tick_elapsed_ms_  = 0;
    uint32_t    tick_count_       = 0;

    // Slave BPM estimation
    uint32_t    last_tick_ms_     = 0;
    uint32_t    slave_bpm_        = 0;
    uint32_t    runtime_ms_       = 0;
};

} // namespace midi