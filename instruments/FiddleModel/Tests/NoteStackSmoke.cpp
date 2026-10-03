#include "Dsp/MidiNoteStack.h"

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
    fiddle::MidiNoteStack notes;
    notes.reset();

    auto s = notes.noteOn(60, 0.50f);
    if (!s.active || s.note != 60 || !s.changed)
        return fail("first note-on should select C4");

    s = notes.noteOn(64, 0.75f);
    if (!s.active || s.note != 64 || !s.changed)
        return fail("newer E4 should become current");

    // Releasing an older/background note must not stop E4.
    s = notes.noteOff(60);
    if (!s.active || s.note != 64 || s.changed)
        return fail("background note-off must not affect current note");

    // Re-press C, then release it: should fall back to held E.
    s = notes.noteOn(60, 0.60f);
    if (s.note != 60 || !s.changed)
        return fail("re-pressed C4 should become current");

    s = notes.noteOff(60);
    if (!s.active || s.note != 64 || !s.changed)
        return fail("releasing current C4 should fall back to held E4");

    s = notes.noteOff(64);
    if (s.active || s.note != -1 || !s.changed)
        return fail("releasing final note should become silent");

    // Last-note priority among several held notes.
    notes.noteOn(55, 0.40f);
    notes.noteOn(62, 0.50f);
    notes.noteOn(69, 0.90f);
    s = notes.noteOff(69);
    if (!s.active || s.note != 62)
        return fail("note stack should restore the most recently pressed held note");

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
