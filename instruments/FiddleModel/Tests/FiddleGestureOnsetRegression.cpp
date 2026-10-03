#include "Dsp/FiddleEngine.h"
#include "Dsp/FiddleGestureProfile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

struct GestureRender
{
    std::vector<float> audio;
    double onsetMs = 0.0;
    double peakRms = 0.0;
};

double windowRms(const std::vector<float>& x,
                 std::size_t begin,
                 std::size_t length)
{
    if (begin >= x.size())
        return 0.0;

    const auto end = std::min(x.size(), begin + length);
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);

    return std::sqrt(sum / std::max<std::size_t>(1, end - begin));
}

GestureRender renderGesture(fiddle::BowAction action, float velocity)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.52f;
    controls.speed = 0.60f;
    controls.attack = 0.52f;
    controls.position = 0.45f;
    controls.balance = -0.90f;
    controls.vibratoWidth = 0.0f;

    const auto profile = fiddle::makeBowGestureProfile(action, velocity);
    controls.pressure = std::clamp(
        controls.pressure + profile.pressureBoost, 0.0f, 1.0f);
    controls.speed = std::clamp(
        controls.speed * profile.speedScale, 0.0f, 1.0f);
    controls.attack = std::clamp(
        controls.attack + profile.responseBoost, 0.0f, 1.0f);
    engine.setControls(controls);
    engine.setStrokeBite(
        profile.biteBoost, profile.biteDurationSeconds);

    std::array<float, 4> fingering {};
    fingering[1] = 329.6276f; // E4 on D
    engine.setFingeringLayout(fingering, 1, 1, velocity);

    switch (action)
    {
        case fiddle::BowAction::AccentStroke:
            engine.startShortStroke(+1, profile.durationSeconds);
            break;
        case fiddle::BowAction::ShortStroke:
            engine.startShortStroke(+1, profile.durationSeconds);
            break;
        case fiddle::BowAction::Chop:
            engine.startChop(+1, profile.durationSeconds);
            break;
        default:
            engine.startBow(+1);
            break;
    }

    GestureRender result;
    result.audio.assign(
        static_cast<std::size_t>(0.16 * sampleRate), 0.0f);
    std::vector<float> right(result.audio.size(), 0.0f);
    engine.process(result.audio.data(), right.data(), result.audio.size());

    constexpr std::size_t window =
        static_cast<std::size_t>(0.002 * sampleRate);
    constexpr std::size_t hop =
        static_cast<std::size_t>(0.0005 * sampleRate);
    const auto analysisEnd =
        std::min(result.audio.size(),
                 static_cast<std::size_t>(0.080 * sampleRate));

    for (std::size_t i = 0; i + window <= analysisEnd; i += hop)
        result.peakRms = std::max(result.peakRms, windowRms(result.audio, i, window));

    const auto threshold = result.peakRms * 0.22;
    result.onsetMs = 80.0;

    for (std::size_t i = 0; i + window <= analysisEnd; i += hop)
    {
        if (windowRms(result.audio, i, window) >= threshold)
        {
            result.onsetMs =
                1000.0 * static_cast<double>(i) / sampleRate;
            break;
        }
    }

    return result;
}

int fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main()
{
    const auto down =
        renderGesture(fiddle::BowAction::DownBow, 0.82f);
    const auto accent =
        renderGesture(fiddle::BowAction::AccentStroke, 0.82f);
    const auto shortStroke =
        renderGesture(fiddle::BowAction::ShortStroke, 0.82f);
    const auto chop =
        renderGesture(fiddle::BowAction::Chop, 0.82f);

    std::cout << "down_onset_ms=" << down.onsetMs
              << " down_peak_rms=" << down.peakRms << '\n'
              << "accent_onset_ms=" << accent.onsetMs
              << " accent_peak_rms=" << accent.peakRms << '\n'
              << "short_onset_ms=" << shortStroke.onsetMs
              << " short_peak_rms=" << shortStroke.peakRms << '\n'
              << "chop_onset_ms=" << chop.onsetMs
              << " chop_peak_rms=" << chop.peakRms << '\n';

    if (!(std::isfinite(down.onsetMs)
          && std::isfinite(accent.onsetMs)
          && down.peakRms > 1.0e-5
          && accent.peakRms > 1.0e-5))
        return fail("gesture onset render was silent or non-finite");

    if (!(accent.onsetMs + 1.0 < down.onsetMs))
        return fail("Accent did not catch the string earlier than ordinary Down Bow");

    if (shortStroke.onsetMs > down.onsetMs + 4.0)
        return fail("Short Stroke onset became too sluggish for fast fiddle articulation");

    if (!(chop.peakRms > 1.0e-5 && chop.onsetMs <= down.onsetMs + 2.0))
        return fail("Chop did not produce a prompt physical contact transient");

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
