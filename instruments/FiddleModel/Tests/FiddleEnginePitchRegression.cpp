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

double correlationAtFrequency(const std::vector<float>& x,
                              std::size_t begin,
                              std::size_t end,
                              double frequency)
{
    begin = std::min(begin, x.size());
    end = std::min(end, x.size());
    if (end <= begin + 100)
        return -1.0;

    const auto lag = sampleRate / frequency;
    const auto lagInt = static_cast<std::size_t>(std::floor(lag));
    const auto frac = lag - static_cast<double>(lagInt);
    if (begin + lagInt + 2 >= end)
        return -1.0;

    double mean = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        mean += x[i];
    mean /= static_cast<double>(end - begin);

    double dot = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    const auto last = end - lagInt - 1;

    for (std::size_t i = begin; i < last; ++i)
    {
        const auto a = static_cast<double>(x[i]) - mean;
        const auto d0 = static_cast<double>(x[i + lagInt]) - mean;
        const auto d1 = static_cast<double>(x[i + lagInt + 1]) - mean;
        const auto b = (1.0 - frac) * d0 + frac * d1;
        dot += a * b;
        aa += a * a;
        bb += b * b;
    }

    return dot / (std::sqrt(aa * bb) + 1.0e-30);
}

double estimateFrequency(const std::vector<float>& x, double target)
{
    constexpr int candidates = 160;
    double bestFrequency = target;
    double bestCorrelation = -2.0;

    for (int i = 0; i <= candidates; ++i)
    {
        const auto fraction = static_cast<double>(i) / candidates;
        const auto frequency = target * (0.985 + 0.030 * fraction);
        const auto corr = correlationAtFrequency(
            x,
            static_cast<std::size_t>(0.80 * sampleRate),
            static_cast<std::size_t>(1.50 * sampleRate),
            frequency);

        if (corr > bestCorrelation)
        {
            bestCorrelation = corr;
            bestFrequency = frequency;
        }
    }

    return bestFrequency;
}

double centsBetween(double measured, double target)
{
    return 1200.0 * std::log2(measured / target);
}

bool testPitch(double target)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.55f;
    controls.speed = 0.60f;
    controls.attack = 0.55f;
    controls.position = 0.45f;
    controls.balance = -1.0f; // strongly favor the fingered D string
    engine.setControls(controls);
    engine.noteOn(static_cast<float>(target), 0.85f);

    std::vector<float> left(static_cast<std::size_t>(1.65 * sampleRate), 0.0f);
    std::vector<float> right(left.size(), 0.0f);
    engine.process(left.data(), right.data(), left.size());

    const auto estimated = estimateFrequency(left, target);
    const auto cents = centsBetween(estimated, target);

    std::cout << "target=" << target
              << " estimated=" << estimated
              << " cents=" << cents << '\n';

    return std::abs(cents) <= 7.0;
}
} // namespace

bool testStringAssignmentSurvivesBend()
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.55f;
    controls.speed = 0.60f;
    controls.attack = 0.55f;
    controls.position = 0.45f;
    controls.balance = 0.0f;
    engine.setControls(controls);

    // A4 selects the A string. A large downward retune must not migrate it to D.
    engine.noteOn(440.0f, 0.85f);
    engine.retune(391.9954f);

    std::vector<float> left(static_cast<std::size_t>(0.35 * sampleRate), 0.0f);
    std::vector<float> right(left.size(), 0.0f);
    engine.process(left.data(), right.data(), left.size());

    const auto debug = engine.debugSnapshot();
    return debug.primaryString == 2
        && debug.bowPairLowerString == 2
        && std::abs(debug.speakingFrequencyHz[2] - 440.0f) < 0.5f;
}

bool testContinuousRetune()
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.55f;
    controls.speed = 0.60f;
    controls.attack = 0.55f;
    controls.position = 0.45f;
    controls.balance = -1.0f;
    engine.setControls(controls);

    engine.noteOn(329.6276f, 0.85f);
    std::vector<float> scratch(static_cast<std::size_t>(0.45 * sampleRate), 0.0f);
    std::vector<float> scratchR(scratch.size(), 0.0f);
    engine.process(scratch.data(), scratchR.data(), scratch.size());

    engine.retune(391.9954f);
    std::fill(scratch.begin(), scratch.end(), 0.0f);
    std::fill(scratchR.begin(), scratchR.end(), 0.0f);
    engine.process(scratch.data(), scratchR.data(), scratch.size());

    const auto debug = engine.debugSnapshot();
    return debug.primaryString == 1
        && debug.bowPairLowerString == 1
        && std::abs(debug.speakingFrequencyHz[1] - 391.9954f) < 0.5f;
}

int main()
{
    constexpr std::array<double, 3> targets {
        329.6276, // E4 on D
        369.9944, // F#4 on D
        391.9954  // G4 on D
    };

    for (const auto target : targets)
    {
        if (!testPitch(target))
        {
            std::cerr << "FAIL: pitch error exceeded 7 cents\n";
            return EXIT_FAILURE;
        }
    }

    if (!testContinuousRetune())
    {
        std::cerr << "FAIL: continuous retune changed string assignment or missed target\n";
        return EXIT_FAILURE;
    }

    if (!testStringAssignmentSurvivesBend())
    {
        std::cerr << "FAIL: pitch bend migrated the note to a different physical string\n";
        return EXIT_FAILURE;
    }

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
