#pragma once

// Extended MIDI (.xmi, Miles Sound System "XMIDI"): an IFF file
//   FORM XDIR { INFO u16 count } CAT XMID { FORM XMID { TIMB, RBRN?, EVNT } ... }
// or a single FORM XMID. IFF chunk sizes are big-endian and padded to even.
// EVNT is a MIDI-like event stream at a fixed 120 ticks per second:
//   - bytes below 0x80 between events are a delay, the sum of consecutive bytes;
//   - a note-on (0x9n key velocity) is followed by its duration as a MIDI
//     variable-length quantity; there are no note-offs;
//   - other channel messages are as in MIDI, without running status;
//   - 0xF0/0xF7 sysex and 0xFF meta events carry a variable-length length.
// Controllers 116/117 are the Miles loop start and end (audio/xmi_player.h).
// The zone's emitters select a sequence by its zero-based index (the EFF
// music selector or EMT field 17, passed unchanged to AIL_init_sequence).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nmsc::assets {

struct XmiEvent {
    std::uint32_t delay = 0;       // ticks after the previous event
    std::uint8_t status = 0;       // MIDI status; 0xFF meta
    std::uint8_t data1 = 0, data2 = 0;
    std::uint8_t meta_type = 0;    // for status 0xFF
    std::uint32_t duration = 0;    // note-on: ticks until its note-off
};

struct XmiSequence {
    std::vector<XmiEvent> events;  // sysex and meta events other than end-of-track are kept as status only
};

inline constexpr int kXmiTicksPerSecond = 120;

// The sequences in file order. Throws std::runtime_error on a malformed file.
std::vector<XmiSequence> parse_xmi(std::span<const std::byte> data);

} // namespace nmsc::assets
