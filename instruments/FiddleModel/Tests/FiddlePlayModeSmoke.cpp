#include "Dsp/FiddlePlayLayout.h"
#include "Dsp/FiddleFingeringVoicer.h"

#include <array>
#include <cmath>
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
        || fiddle::bowActionForMidiNote(37) != BowAction::Shuffle
        || fiddle::bowActionForMidiNote(38) != BowAction::UpBow
        || fiddle::bowActionForMidiNote(40) != BowAction::ShortStroke
        || fiddle::bowActionForMidiNote(41) != BowAction::Tremolo
        || fiddle::bowActionForMidiNote(43) != BowAction::DroneBow
        || fiddle::bowActionForMidiNote(45) != BowAction::AccentStroke
        || fiddle::bowActionForMidiNote(46) != BowAction::Chop
        || fiddle::bowActionForMidiNote(47) != BowAction::Release)
        return fail("Bow Action key map changed unexpectedly");

    if (!fiddle::isFingeringKey(55) || fiddle::isFingeringKey(54)
        || !fiddle::isFingeringKey(108) || fiddle::isFingeringKey(109))
        return fail("Fingering region must be G3 through C8");

    const auto g3Mask = fiddle::fingeringMaskBit(55);
    const auto e4Mask = fiddle::fingeringMaskBit(64);
    const auto c8Mask = fiddle::fingeringMaskBit(108);
    if (g3Mask == 0 || e4Mask == 0 || c8Mask == 0
        || g3Mask == e4Mask || e4Mask == c8Mask
        || fiddle::fingeringMaskBit(54) != 0
        || fiddle::fingeringMaskBit(109) != 0)
        return fail("Fingering visual mask must map each G3-C8 key to one distinct bit");

    {
        std::array<int, 4> notes { 64, -1, -1, -1 }; // E4
        const auto layout = fiddle::voiceFingering(notes, 1, 64);
        if (layout.midiNoteByString[1] != 64 || layout.primaryString != 1)
            return fail("E4 should be stopped on D string");
        if (fiddle::singleStringFocusForLayout(layout, 1) > -0.90f)
            return fail("monophonic E4 should auto-focus the D string");
    }

    {
        // Keep A4 on D as a fourth-finger note when continuing a D-string phrase.
        std::array<int, 4> notes { 69, -1, -1, -1 }; // A4
        const auto layout = fiddle::voiceFingering(notes, 1, 69, 1);
        if (layout.midiNoteByString[1] != 69 || layout.primaryString != 1)
            return fail("A4 should stay on D when D is the preferred phrase string");
    }

    {
        // B4 is outside the low-position span on D, so move naturally to A.
        std::array<int, 4> notes { 71, -1, -1, -1 }; // B4
        const auto layout = fiddle::voiceFingering(notes, 1, 71, 1);
        if (layout.midiNoteByString[2] != 71 || layout.primaryString != 2)
            return fail("B4 should move to A after the practical D-string span");
    }

    {
        std::array<int, 4> notes { 64, 71, -1, -1 }; // E4 + B4
        const auto layout = fiddle::voiceFingering(notes, 2, 71);
        if (layout.midiNoteByString[1] != 64
            || layout.midiNoteByString[2] != 71
            || layout.bowPairLowerString != 1)
            return fail("E4+B4 should voice as stopped D+A double stop");
        if (std::abs(fiddle::singleStringFocusForLayout(layout, 2)) > 1.0e-6f)
            return fail("double-stop fingering should keep centre focus available");
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
