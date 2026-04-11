#include "platform/midi/midi_parser.h"

namespace midi
{

void MidiParser::Init()
{
    message_       = MidiMessage{};
    bytes_needed_  = 0;
    bytes_read_    = 0;
    message_ready_ = false;

    for (int i = 0; i < 3; ++i) buffer_[i] = 0;
}

bool MidiParser::ParseByte(uint8_t byte)
{
    message_ready_ = false;

    // ── Realtime messages — single byte, no data ──────────────────────────────
    // These can appear anywhere in the stream, even mid-message
    if (byte == 0xF8 || byte == 0xFA ||
        byte == 0xFB || byte == 0xFC)
    {
        message_.type    = static_cast<MidiMessageType>(byte);
        message_.channel = 0;
        message_.data1   = 0;
        message_.data2   = 0;
        message_ready_   = true;
        return true;
    }

    // ── Status byte ───────────────────────────────────────────────────────────
    if (byte & 0x80)
    {
        buffer_[0]    = byte;
        bytes_read_   = 1;
        bytes_needed_ = GetMessageLength(byte);
        return false;
    }

    // ── Data byte ─────────────────────────────────────────────────────────────
    if (bytes_needed_ == 0) return false;   // no status received yet

    buffer_[bytes_read_++] = byte;

    if (bytes_read_ >= bytes_needed_)
    {
        // Complete message
        uint8_t status = buffer_[0];

        message_.type    = static_cast<MidiMessageType>(status & 0xF0);
        message_.channel = status & 0x0F;
        message_.data1   = (bytes_needed_ > 1) ? buffer_[1] : 0;
        message_.data2   = (bytes_needed_ > 2) ? buffer_[2] : 0;
        message_ready_   = true;
        bytes_read_      = 1;   // running status — keep status byte
        return true;
    }

    return false;
}

const MidiMessage& MidiParser::GetMessage() const
{
    return message_;
}

bool MidiParser::IsRealtime() const
{
    return message_.type == MidiMessageType::Clock   ||
           message_.type == MidiMessageType::Start   ||
           message_.type == MidiMessageType::Stop    ||
           message_.type == MidiMessageType::Continue;
}

uint8_t MidiParser::GetMessageLength(uint8_t status) const
{
    uint8_t type = status & 0xF0;

    switch (type)
    {
        case 0x80: return 3;   // Note Off
        case 0x90: return 3;   // Note On
        case 0xA0: return 3;   // Aftertouch
        case 0xB0: return 3;   // Control Change
        case 0xC0: return 2;   // Program Change
        case 0xD0: return 2;   // Channel Pressure
        case 0xE0: return 3;   // Pitch Bend
        default:   return 1;   // System / realtime
    }
}

} // namespace midi