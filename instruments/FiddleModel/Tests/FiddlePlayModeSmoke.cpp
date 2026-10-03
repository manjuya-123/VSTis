#include "Dsp/FiddlePlayLayout.h"
#include "Dsp/FiddleFingeringVoicer.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{
int fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main()
{
    using fiddle::BowAction;

    if (fiddle::bowActionForMidiNote(36) != BowAction::DownBow
        || fiddle::bowActionForMidiNote(38) != BowAction::UpBow
        || fiddle::bowActionForMidiNote(40) != BowAction::ShortStroke
        || fiddle::bowActionForMidiNote(41) != BowAction::Tremolo
        || fiddle::bowActionForMidiNote(43) != BowAction::DroneBow
        || fiddle::bowActionForMidiNote(45) != BowAction::AccentStroke
        || fiddle::bowActionForMidiNote(47) != BowAction::Release)
        return fail("Bow Action key map changed unexpectedly");

    if (!fiddle::isFingeringKey(55) || fiddle::isFingeringKey(54))
        return fail("Fingering region must begin at G3");

    {
        std::array<int, 4> notes { 64, -1, -1, -1 }; // E4
        const auto layout = fiddle::voiceFingering(notes, 1, 64);
        if (layout.midiNoteByString[1] != 64 || layout.primaryString != 1)
            return fail("E4 should be stopped on D string");
    }

    {
        std::array<int, 4> notes { 64, 71, -1, -1 }; // E4 + B4
        const auto layout = fiddle::voiceFingering(notes, 2, 71);
        if (layout.midiNoteByString[1] != 64
            || layout.midiNoteByString[2] != 71
            || layout.bowPairLowerString != 1)
            return fail("E4+B4 should voice as stopped D+A double stop");
    }

    {
        std::array<int, 4> notes { 55, 62, 69, 76 };
        const auto layout = fiddle::voiceFingering(notes, 4, 76);
        for (int i = 0; i < 4; ++i)
            if (layout.midiNoteByString[static_cast<std::size_t>(i)]
                != fiddle::openStringMidi[static_cast<std::size_t>(i)])
                return fail("open G/D/A/E chord should map one note per string");
    }

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
