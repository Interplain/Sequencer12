#include "platform/midi/midi_clock.h"
#include <iostream>

namespace midi
{

void MidiClock::Init(ClockMode mode, MidiClockListener* listener)
{
    mode_            = mode;
    state_           = ClockState::Stopped;
    listener_        = listener;
    bpm_             = 120;
    tick_elapsed_ms_ = 0;
    tick_count_      = 0;
    last_tick_ms_    = 0;
    slave_bpm_       = 0;
    runtime_ms_      = 0;

    parser_.Init();
    SetBpm(bpm_);

    std::cout
        << "[MIDI CLOCK] Init "
        << (mode == ClockMode::Master ? "MASTER" : "SLAVE")
        << " BPM=" << bpm_
        << std::endl;
}

// ── BPM ──────────────────────────────────────────────────────────────────────

void MidiClock::SetBpm(uint32_t bpm)
{
    if (bpm == 0) bpm = 1;
    bpm_ = bpm;

    // 24 ticks per quarter note
    // quarter note ms = 60000 / bpm
    // tick interval   = quarter_ms / 24
    tick_interval_ms_ = (60000 / bpm) / kPPQN;
    if (tick_interval_ms_ == 0) tick_interval_ms_ = 1;
}

uint32_t MidiClock::GetBpm() const
{
    return bpm_;
}

// ── Transport ─────────────────────────────────────────────────────────────────

void MidiClock::Start()
{
    if (mode_ != ClockMode::Master) return;

    state_           = ClockState::Playing;
    tick_elapsed_ms_ = 0;
    tick_count_      = 0;

    SendStart();

    std::cout << "[MIDI CLOCK] START sent" << std::endl;
}

void MidiClock::Stop()
{
    if (mode_ != ClockMode::Master) return;

    state_ = ClockState::Stopped;

    SendStop();

    std::cout << "[MIDI CLOCK] STOP sent" << std::endl;
}

void MidiClock::Continue()
{
    if (mode_ != ClockMode::Master) return;

    state_ = ClockState::Continuing;

    SendContinue();

    std::cout << "[MIDI CLOCK] CONTINUE sent" << std::endl;
}

// ── Tick1ms ───────────────────────────────────────────────────────────────────

void MidiClock::Tick1ms()
{
    ++runtime_ms_;

    if (mode_ != ClockMode::Master) return;
    if (state_ == ClockState::Stopped) return;

    ++tick_elapsed_ms_;

    if (tick_elapsed_ms_ >= tick_interval_ms_)
    {
        tick_elapsed_ms_ = 0;
        ++tick_count_;

        SendClock();

        if (listener_)
        {
            listener_->OnClockTick();
        }
    }
}

// ── Slave ─────────────────────────────────────────────────────────────────────

void MidiClock::ReceiveByte(uint8_t byte)
{
    if (mode_ != ClockMode::Slave) return;

    if (parser_.ParseByte(byte))
    {
        const MidiMessage& msg = parser_.GetMessage();

        switch (msg.type)
        {
            case MidiMessageType::Clock:
            {
                // Estimate BPM from tick interval
                if (last_tick_ms_ > 0)
                {
                    uint32_t interval = runtime_ms_ - last_tick_ms_;
                    UpdateSlaveBpm(interval);
                }

                last_tick_ms_ = runtime_ms_;

                if (state_ == ClockState::Playing ||
                    state_ == ClockState::Continuing)
                {
                    if (listener_) listener_->OnClockTick();
                }
                break;
            }

            case MidiMessageType::Start:
                state_ = ClockState::Playing;
                if (listener_) listener_->OnClockStart();
                std::cout << "[MIDI CLOCK] SLAVE START received" << std::endl;
                break;

            case MidiMessageType::Stop:
                state_ = ClockState::Stopped;
                if (listener_) listener_->OnClockStop();
                std::cout << "[MIDI CLOCK] SLAVE STOP received" << std::endl;
                break;

            case MidiMessageType::Continue:
                state_ = ClockState::Continuing;
                if (listener_) listener_->OnClockContinue();
                std::cout << "[MIDI CLOCK] SLAVE CONTINUE received" << std::endl;
                break;

            default:
                break;
        }
    }
}

// ── Mode ─────────────────────────────────────────────────────────────────────

void MidiClock::SetMode(ClockMode mode)
{
    mode_  = mode;
    state_ = ClockState::Stopped;

    std::cout
        << "[MIDI CLOCK] Mode = "
        << (mode == ClockMode::Master ? "MASTER" : "SLAVE")
        << std::endl;
}

ClockMode MidiClock::GetMode() const
{
    return mode_;
}

ClockState MidiClock::GetState() const
{
    return state_;
}

uint32_t MidiClock::GetSlaveBpm() const
{
    return slave_bpm_;
}

// ── Send helpers ──────────────────────────────────────────────────────────────

void MidiClock::SendByte(uint8_t byte)
{
    // Sim stub — on hardware: HAL_UART_Transmit(&huart2, &byte, 1, 1);
    std::cout
        << "[MIDI TX] 0x"
        << std::hex << static_cast<int>(byte)
        << std::dec
        << std::endl;
}

void MidiClock::SendClock()
{
    SendByte(0xF8);
}

void MidiClock::SendStart()
{
    SendByte(0xFA);
}

void MidiClock::SendStop()
{
    SendByte(0xFC);
}

void MidiClock::SendContinue()
{
    SendByte(0xFB);
}

// ── Slave BPM estimation ──────────────────────────────────────────────────────

void MidiClock::UpdateSlaveBpm(uint32_t tick_interval_ms)
{
    if (tick_interval_ms == 0) return;

    // BPM = 60000 / (tick_interval_ms * 24)
    uint32_t estimated = 60000 / (tick_interval_ms * kPPQN);

    if (estimated > 0)
    {
        // Simple smoothing — average with previous estimate
        if (slave_bpm_ == 0)
            slave_bpm_ = estimated;
        else
            slave_bpm_ = (slave_bpm_ * 3 + estimated) / 4;
    }
}

} // namespace midi