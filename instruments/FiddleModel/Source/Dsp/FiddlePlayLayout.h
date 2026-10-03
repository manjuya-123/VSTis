#pragma once

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

inline constexpr int fiddleLowestNote = 55; // G3

inline BowAction bowActionForMidiNote(int midiNote) noexcept
{
    // White keys from C2 to B2 form the right-hand action row.
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
    return midiNote >= fiddleLowestNote && midiNote <= 108;
}
} // namespace fiddle
