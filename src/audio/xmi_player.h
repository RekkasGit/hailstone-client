#pragma once

// Plays an XMI sequence (assets/xmi.h) through TinySoundFont with a General
// MIDI SoundFont; the game ships synthusr.sf2 next to the synthusr.dls the
// original's Miles synthesiser loads. The timing follows the Miles sequencer
// (rof2decomp refs/openeq docs/XMI_NATIVE_SELECTION.md, mss32 @0x21116850):
// 120 ticks a second; each tick first counts down the 32 note slots and
// releases the expired notes in slot order, then plays the events due; a
// zero-duration note lasts one tick; the end of the track stops the notes;
// controllers 116/117 open and close up to four loops (count 0 = forever).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "assets/xmi.h"
#include "audio/pcm.h"

struct tsf;

namespace nmsc::audio {

// A loaded SoundFont shared by the music sources (each plays on its own copy).
class SoundFont {
public:
    static std::shared_ptr<SoundFont> load(std::span<const std::byte> sf2);
    ~SoundFont();
    tsf* get() const { return font_; }

private:
    explicit SoundFont(tsf* f) : font_(f) {}
    tsf* font_;
};

// The sequencer alone (no synth): what a tick plays. For tests and the source below.
class XmiSequencer {
public:
    struct Event {
        enum class Kind { NoteOn, NoteOff, Channel, End } kind;
        std::uint8_t status = 0, data1 = 0, data2 = 0;
    };
    // The most events one tick() appends (xmi_player.cpp): the source reserves it, so the audio thread does not
    // allocate.
    static constexpr std::size_t kMaxTickOutput = 4096 + 32 * 4 + 1;
    XmiSequencer(assets::XmiSequence seq, std::int32_t loops);
    // Advances one tick and appends what to play to `out`. After the end
    // (loops used up) only returns End once.
    void tick(std::vector<Event>& out);
    bool finished() const { return finished_; }
    // Whether the sequence was stopped for running too many events in one tick (logged by its owner: tick() runs on
    // the audio thread, which does not write the log).
    bool overran() const { return overran_; }

private:
    struct Slot {
        int channel = -1;
        std::uint8_t key = 0;
        std::int64_t remaining = 0;
    };
    struct Loop {
        std::int32_t count = 0;
        std::size_t start = 0;
        bool used = false;
    };
    void restart(std::vector<Event>& out);

    assets::XmiSequence seq_;
    std::int32_t loops_;
    std::size_t next_ = 0;
    std::uint32_t wait_ = 0;
    Slot slots_[32];
    Loop for_[4];
    bool finished_ = false;
    bool overran_ = false;
};

// An XMI sequence as a mixer source (stereo at the device rate).
std::unique_ptr<Source> make_xmi_source(std::shared_ptr<SoundFont> font, assets::XmiSequence seq, std::int32_t loops);

} // namespace nmsc::audio
