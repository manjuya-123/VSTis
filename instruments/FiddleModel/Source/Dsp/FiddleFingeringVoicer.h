#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>

namespace fiddle
{
struct FingeringLayout
{
    std::array<int, 4> midiNoteByString { -1, -1, -1, -1 };
    int primaryString = 1;
    int bowPairLowerString = 1;
};

inline constexpr std::array<int, 4> openStringMidi { 55, 62, 69, 76 }; // G3 D4 A4 E5

inline FingeringLayout voiceFingering(const std::array<int, 4>& inputNotes,
                                      std::size_t noteCount,
                                      int newestNote) noexcept
{
    FingeringLayout result;

    noteCount = std::min<std::size_t>(noteCount, inputNotes.size());
    std::array<int, 4> notes = inputNotes;
    std::sort(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(noteCount),
              std::greater<int>());

    std::array<bool, 4> used { false, false, false, false };

    for (std::size_t noteIndex = 0; noteIndex < noteCount; ++noteIndex)
    {
        const auto note = notes[noteIndex];

        for (int stringIndex = 3; stringIndex >= 0; --stringIndex)
        {
            if (!used[static_cast<std::size_t>(stringIndex)]
                && note >= openStringMidi[static_cast<std::size_t>(stringIndex)])
            {
                result.midiNoteByString[static_cast<std::size_t>(stringIndex)] = note;
                used[static_cast<std::size_t>(stringIndex)] = true;
                break;
            }
        }
    }

    int newestString = -1;
    for (int stringIndex = 0; stringIndex < 4; ++stringIndex)
    {
        if (result.midiNoteByString[static_cast<std::size_t>(stringIndex)] == newestNote)
        {
            newestString = stringIndex;
            break;
        }
    }

    if (newestString < 0)
    {
        for (int stringIndex = 3; stringIndex >= 0; --stringIndex)
        {
            if (newestNote >= openStringMidi[static_cast<std::size_t>(stringIndex)])
            {
                newestString = stringIndex;
                break;
            }
        }
    }

    result.primaryString = std::clamp(newestString < 0 ? 1 : newestString, 0, 3);

    // Prefer an adjacent pair that actually contains two held/fingered notes.
    int selectedPair = std::clamp(result.primaryString, 0, 2);
    for (int pair = 0; pair < 3; ++pair)
    {
        const bool lowerAssigned =
            result.midiNoteByString[static_cast<std::size_t>(pair)] >= 0;
        const bool upperAssigned =
            result.midiNoteByString[static_cast<std::size_t>(pair + 1)] >= 0;

        if (lowerAssigned && upperAssigned)
        {
            selectedPair = pair;
            if (pair == result.primaryString || pair + 1 == result.primaryString)
                break;
        }
    }

    result.bowPairLowerString = selectedPair;
    return result;
}
} // namespace fiddle
