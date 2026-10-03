#include "Dsp/FiddleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr double noteSeconds = 0.090;

double rms(const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    begin = std::min(begin, x.size());
    end = std::min(end, x.size());
    if (end <= begin)
        return 0.0;

    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return std::sqrt(sum / static_cast<double>(end - begin));
}

int fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main()
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.56f;
    controls.speed = 0.68f;
    controls.attack = 0.82f;   // Quick bow response for fast fiddle strokes
    controls.position = 0.48f;
    controls.balance = -0.82f; // Melody-heavy, open A still physically present
    controls.vibratoWidth = 0.0f;
    controls.vibratoPace = 0.5f;
    engine.setControls(controls);

    constexpr std::array<float, 16> phrase {
        329.6276f, 369.9944f, 391.9954f, 369.9944f,
        329.6276f, 369.9944f, 391.9954f, 440.0000f,
        493.8833f, 440.0000f, 391.9954f, 369.9944f,
        329.6276f, 391.9954f, 369.9944f, 329.6276f
    };

    const auto samplesPerNote = static_cast<std::size_t>(noteSeconds * sampleRate);
    std::vector<float> left(samplesPerNote * phrase.size(), 0.0f);
    std::vector<float> right(left.size(), 0.0f);

    int expectedDirection = 1;

    for (std::size_t noteIndex = 0; noteIndex < phrase.size(); ++noteIndex)
    {
        engine.beginBowStroke(true);
        engine.noteOn(phrase[noteIndex], 0.88f);

        const auto offset = noteIndex * samplesPerNote;
        engine.process(left.data() + offset, right.data() + offset, samplesPerNote);

        const auto debug = engine.debugSnapshot();
        if (debug.bowDirection != expectedDirection)
            return fail("alternate bow direction did not toggle on each new note");

        expectedDirection = -expectedDirection;

        // Ignore the first 25 ms containing the physical direction reversal.
        const auto segmentBegin = offset + static_cast<std::size_t>(0.025 * sampleRate);
        const auto segmentEnd = offset + samplesPerNote;
        const auto segmentRms = rms(left, segmentBegin, segmentEnd);

        if (!std::isfinite(segmentRms) || segmentRms < 8.0e-5)
        {
            std::cerr << "note_index=" << noteIndex
                      << " frequency=" << phrase[noteIndex]
                      << " rms=" << segmentRms << '\n';
            return fail("fast alternate passage contains a weak or failed note");
        }
    }

    for (const auto sample : left)
        if (!std::isfinite(sample) || std::abs(sample) > 8.0f)
            return fail("fast passage produced non-finite or runaway audio");

    engine.noteOff();

    std::cout << "PASS\n"
              << "notes=" << phrase.size() << '\n'
              << "note_ms=" << noteSeconds * 1000.0 << '\n';

    return EXIT_SUCCESS;
}
