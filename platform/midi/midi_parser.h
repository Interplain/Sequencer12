#pragma once
#include <cstdint>

namespace midi
{

// ─────────────────────────────────────────────
// MIDI message types we care about
// ─────────────────────────────────────────────

enum class MidiMessageType : uint8_t
{
    None        = 0x00,
    NoteOff     = 0x80,
    NoteOn      = 0x90,
    Clock       = 0xF8,   // 24ppqn tick
    Start       = 0xFA,
    Continue    = 0xFB,
    Stop        = 0xFC,
};

struct MidiMessage
{
    MidiMessageType type    = MidiMessageType::None;
    uint8_t         channel = 0;    // 0-15
    uint8_t         data1   = 0;    // note number / controller
    uint8_t         data2   = 0;    // velocity / value
};

// ─────────────────────────────────────────────
// Parser
// ─────────────────────────────────────────────

class MidiParser
{
public:
    void Init();

    // Feed one byte from UART RX
    // Returns true if a complete message is ready
    bool ParseByte(uint8_t byte);

    // Get the last parsed message
    const MidiMessage& GetMessage() const;

    // Returns true if last message was a realtime message
    // (Clock, Start, Stop, Continue)
    bool IsRealtime() const;

private:
    MidiMessage message_{};
    uint8_t     buffer_[3]    = {};
    uint8_t     bytes_needed_ = 0;
    uint8_t     bytes_read_   = 0;
    bool        message_ready_ = false;

    uint8_t GetMessageLength(uint8_t status) const;
};

} // namespace midi