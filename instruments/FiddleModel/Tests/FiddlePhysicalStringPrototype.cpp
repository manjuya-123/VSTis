#include "Dsp/experimental/ContinuousStringCore.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

using fiddle::experimental::ContinuousStringCore;

namespace
{
bool passiveWaveTest(double sampleRate)
{
    ContinuousStringCore string;
    string.prepare(sampleRate, 440.0);
    string.setDamping(0.0);
    string.seedFundamentalForTest(0.00002);

    const auto initial = string.freeStringEnergy();
    if (!(initial > 0.0) || !(string.courantNumber() <= 0.90))
        return false;

    double largestRelativeError = 0.0;
    double previousBridgeForce = 0.0;
    int risingCrossings = 0;
    for (int sample = 0; sample < static_cast<int>(sampleRate * 0.20); ++sample)
    {
        const auto force = string.step(0.0, 0.0);
        const auto energy = string.freeStringEnergy();
        if (!std::isfinite(energy) || !std::isfinite(force))
            return false;
        largestRelativeError = std::max(largestRelativeError,
                                        std::abs(energy / initial - 1.0));
        if (sample > static_cast<int>(sampleRate * 0.04)
            && previousBridgeForce <= 0.0 && force > 0.0)
            ++risingCrossings;
        previousBridgeForce = force;
    }
    const auto measuredHz = risingCrossings / 0.16;
    std::cout << sampleRate << " Hz, substeps=" << string.internalSubsteps()
              << ", lambda=" << string.courantNumber()
              << ", energy error=" << largestRelativeError
              << ", estimated f0=" << measuredHz << '\n';
    return largestRelativeError < 0.004
        && std::abs(measuredHz - 440.0) < 12.0;
}

bool fingerContinuityTest()
{
    ContinuousStringCore string;
    string.prepare(48000.0, 440.0);
    string.seedFundamentalForTest(0.00002);
    for (int sample = 0; sample < 1000; ++sample)
        string.step(0.0, 0.0);

    const auto before = string.displacementAt(0.60);
    string.setFingerFrequency(587.3295);
    const auto immediatelyAfterCommand = string.displacementAt(0.60);
    if (before != immediatelyAfterCommand)
        return false; // MIDI command MUST NOT write to the string state.

    const auto firstBridgeForce = string.step(0.0, 0.0);
    if (!std::isfinite(firstBridgeForce))
        return false;
    for (int sample = 0; sample < 5000; ++sample)
    {
        const auto output = string.step(0.0, 0.0);
        if (!std::isfinite(output) || !std::isfinite(string.freeStringEnergy())
            || std::abs(output) > 1000.0)
            return false;
    }
    string.setFingerFrequency(440.0);
    for (int sample = 0; sample < 2000; ++sample)
        if (!std::isfinite(string.step(0.0, 0.0)))
            return false;
    return true;
}

bool boundedBowTest(double frequency)
{
    ContinuousStringCore string;
    string.prepare(48000.0, frequency);
    double totalAbsoluteOutput = 0.0;
    int sticks = 0;
    int slides = 0;
    for (int sample = 0; sample < 16000; ++sample)
    {
        if (sample == 5500)
            string.setFingerFrequency(frequency * std::pow(2.0, 2.0 / 12.0));
        if (sample == 11000)
            string.setFingerFrequency(frequency * std::pow(2.0, 4.0 / 12.0));
        const auto output = string.step(0.25, 0.30);
        if (!std::isfinite(output) || !std::isfinite(string.bowForceN())
            || !std::isfinite(string.fingerForceN())
            || std::abs(output) > 1000.0)
            return false;
        totalAbsoluteOutput += std::abs(output);
        if (string.sticking())
            ++sticks;
        else
            ++slides;
    }
    std::cout << frequency << " Hz bow: mean |bridge force|="
              << totalAbsoluteOutput / 16000.0
              << ", stick samples=" << sticks
              << ", slip samples=" << slides << '\n';
    return totalAbsoluteOutput > 1.0e-8 && sticks > 0 && slides > 0;
}
} // namespace

int main()
{
    const bool ok = passiveWaveTest(44100.0)
        && passiveWaveTest(48000.0)
        && fingerContinuityTest()
        && boundedBowTest(196.0)
        && boundedBowTest(440.0)
        && boundedBowTest(659.2551);
    std::cout << (ok ? "physical string prototype: PASS"
                     : "physical string prototype: FAIL") << '\n';
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
