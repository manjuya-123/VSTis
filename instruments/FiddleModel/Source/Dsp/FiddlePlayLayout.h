#pragma once

#include <cstdint>

namespace fiddle
{
enum class PlayMode
{
    Chromatic = 0,
    FiddlePlay
};

enum class BowAction
{
    None = 0,
    DownBow,
    UpBow,
    ShortStroke,
    Tremolo,
    DroneBow,
    AccentStroke,
    Chop,
    Release,
    Shuffle
};

inline constexpr int fiddleLowestNote = 55;   // G3
inline constexpr int fiddleHighestNote = 108; // C8
inline constexpr int fiddleFingeringKeyCount =
    fiddleHighestNote - fiddleLowestNote + 1;
static_assert(fiddleFingeringKeyCount <= 64);

inline BowAction bowActionForMidiNote(int midiNote) noexcept
{
    // C2-B2 form the right-hand action row; C#2 carries Nashville Shuffle.
    switch (midiNote)
    {
        case 36: return BowAction::DownBow;       // C2
        case 37: return BowAction::Shuffle;       // C#2
        case 38: return BowAction::UpBow;         // D2
        case 40: return BowAction::ShortStroke;   // E2
        case 41: return BowAction::Tremolo;       // F2
        case 43: return BowAction::DroneBow;      // G2
        case 45: return BowAction::AccentStroke;  // A2
        case 46: return BowAction::Chop;          // A#2
        case 47: return BowAction::Release;       // B2
        default: return BowAction::None;
    }
}

inline bool isBowActionKey(int midiNote) noexcept
{
    return bowActionForMidiNote(midiNote) != BowAction::None;
}

inline bool isFingeringKey(int midiNote) noexcept
{
    return midiNote >= fiddleLowestNote && midiNote <= fiddleHighestNote;
}

inline constexpr std::uint64_t fingeringMaskBit(int midiNote) noexcept
{
    return midiNote >= fiddleLowestNote && midiNote <= fiddleHighestNote
        ? (std::uint64_t { 1 }
           << static_cast<unsigned>(midiNote - fiddleLowestNote))
        : std::uint64_t { 0 };
}
} // namespace fiddle
