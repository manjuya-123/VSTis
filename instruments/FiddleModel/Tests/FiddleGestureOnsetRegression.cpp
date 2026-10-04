#include "Dsp/FiddleEngine.h"
#include "Dsp/FiddleGestureProfile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
    double earlyRms = 0.0;
};

struct ReversalMetrics
{
    int reversals = 0;
    double maxLatencyMs = 0.0;
    double meanLatencyMs = 0.0;
    double minIntervalMs = 0.0;
    double maxIntervalMs = 0.0;
    double intervalRatio = 1.0;
};

struct ReleaseMetrics
{
    double halfLiftMs = 1000.0;
    double tenPercentLiftMs = 1000.0;
    double endLiftMs = 1000.0;
    double bowSpeedAtEndMps = 0.0;
    bool sawActive = false;
    bool endedInactive = false;
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
        case fiddle::BowAction::ShortStroke:
            engine.startShortStroke(
                +1,
                profile.durationSeconds,
                profile.liftDurationSeconds,
                profile.liftBrake,
                profile.liftForceCurve);
            break;
        case fiddle::BowAction::Chop:
            engine.startChop(
                +1,
                profile.durationSeconds,
                profile.impactVelocityMps,
                profile.impactDurationSeconds);
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

    result.earlyRms = windowRms(
        result.audio,
        0,
        static_cast<std::size_t>(0.020 * sampleRate));

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

ReleaseMetrics measureOneShotRelease(fiddle::BowAction action,
                                      float velocity)
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
    fingering[1] = 329.6276f;
    engine.setFingeringLayout(fingering, 1, 1, velocity);
    engine.startShortStroke(
        +1,
        profile.durationSeconds,
        profile.liftDurationSeconds,
        profile.liftBrake,
        profile.liftForceCurve);

    ReleaseMetrics result;
    bool releaseStarted = false;
    std::int64_t releaseStartSample = -1;
    const auto maxSamples = static_cast<std::int64_t>(
        (profile.durationSeconds + profile.liftDurationSeconds + 0.040f)
        * sampleRate);

    for (std::int64_t sample = 0; sample < maxSamples; ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);

        const auto state = engine.debugSnapshot();
        result.sawActive = result.sawActive || state.oneShotActive;

        if (!releaseStarted && state.oneShotLiftGain < 0.999f)
        {
            releaseStarted = true;
            releaseStartSample = sample;
        }

        if (!releaseStarted)
            continue;

        const auto elapsedMs =
            1000.0 * static_cast<double>(sample - releaseStartSample + 1)
            / sampleRate;

        if (result.halfLiftMs >= 999.0
            && state.oneShotLiftGain <= 0.5f)
            result.halfLiftMs = elapsedMs;

        if (result.tenPercentLiftMs >= 999.0
            && state.oneShotLiftGain <= 0.1f)
            result.tenPercentLiftMs = elapsedMs;

        if (state.oneShotLiftGain <= 0.0f)
        {
            result.endLiftMs = elapsedMs;
            result.bowSpeedAtEndMps = std::abs(state.bowSpeedMps);
            result.endedInactive = !state.oneShotActive;
            break;
        }
    }

    return result;
}

ReversalMetrics measureReversalCatch(fiddle::BowAction action,
                                     float velocity)
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
    fingering[1] = 329.6276f;
    engine.setFingeringLayout(fingering, 1, 1, velocity);

    if (action == fiddle::BowAction::Tremolo)
        engine.startTremolo(profile.tremoloReversalsPerSecond);
    else
        engine.startShuffle(profile.shuffleSubdivisionsPerSecond);

    ReversalMetrics result;
    auto previousDirection = engine.debugSnapshot().bowDirection;
    std::int64_t pendingReversalSample = -1;
    std::int64_t previousReversalSample = 0;
    double latencySumMs = 0.0;
    double minIntervalMs = 1000.0;
    double maxIntervalMs = 0.0;

    constexpr auto speedCatchThreshold = 0.08f;
    const auto maxSamples =
        static_cast<std::int64_t>(0.75 * sampleRate);

    for (std::int64_t sample = 0; sample < maxSamples; ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);

        const auto state = engine.debugSnapshot();
        if (state.bowDirection != previousDirection)
        {
            const auto intervalMs =
                1000.0
                * static_cast<double>(
                    sample - previousReversalSample + 1)
                / sampleRate;
            minIntervalMs = std::min(minIntervalMs, intervalMs);
            maxIntervalMs = std::max(maxIntervalMs, intervalMs);
            previousReversalSample = sample + 1;

            // A second scheduled reversal before the bow has acquired useful
            // speed in the new direction is a failed re-catch.
            if (pendingReversalSample >= 0)
            {
                result.maxLatencyMs = 1000.0;
                return result;
            }

            pendingReversalSample = sample;
            previousDirection = state.bowDirection;
        }

        if (pendingReversalSample >= 0
            && state.bowSpeedMps
                * static_cast<float>(state.bowDirection)
                >= speedCatchThreshold)
        {
            const auto latencyMs =
                1000.0
                * static_cast<double>(
                    sample - pendingReversalSample + 1)
                / sampleRate;
            result.maxLatencyMs =
                std::max(result.maxLatencyMs, latencyMs);
            latencySumMs += latencyMs;
            ++result.reversals;
            pendingReversalSample = -1;

            if (result.reversals >= 5)
                break;
        }
    }

    if (result.reversals > 0)
        result.meanLatencyMs =
            latencySumMs / static_cast<double>(result.reversals);

    if (maxIntervalMs > 0.0 && minIntervalMs < 999.0)
    {
        result.minIntervalMs = minIntervalMs;
        result.maxIntervalMs = maxIntervalMs;
        result.intervalRatio = maxIntervalMs / minIntervalMs;
    }

    return result;
}

void writeU16(std::ofstream& out, std::uint16_t value)
{
    const char bytes[2] {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)
    };
    out.write(bytes, 2);
}

void writeU32(std::ofstream& out, std::uint32_t value)
{
    const char bytes[4] {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)
    };
    out.write(bytes, 4);
}

bool writeComparisonWav(const std::filesystem::path& path,
                        const std::vector<GestureRender>& renders)
{
    std::vector<float> combined;
    const auto gap = static_cast<std::size_t>(0.12 * sampleRate);
    for (const auto& render : renders)
    {
        combined.insert(combined.end(), render.audio.begin(), render.audio.end());
        combined.insert(combined.end(), gap, 0.0f);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;

    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bits = 16;
    const auto frames = static_cast<std::uint32_t>(combined.size());
    const auto dataBytes = frames * channels * (bits / 8u);

    out.write("RIFF", 4); writeU32(out, 36u + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4); writeU32(out, 16u);
    writeU16(out, 1u); writeU16(out, channels);
    writeU32(out, static_cast<std::uint32_t>(sampleRate));
    writeU32(out, static_cast<std::uint32_t>(sampleRate) * 4u);
    writeU16(out, 4u); writeU16(out, bits);
    out.write("data", 4); writeU32(out, dataBytes);

    constexpr float gain = 0.18f;
    for (const auto sample : combined)
    {
        const auto x = std::clamp(sample * gain, -1.0f, 1.0f);
        const auto encoded = static_cast<std::int16_t>(
            std::lrint(x * 32767.0f));
        writeU16(out, static_cast<std::uint16_t>(encoded));
        writeU16(out, static_cast<std::uint16_t>(encoded));
    }

    return static_cast<bool>(out);
}

int fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main(int argc, char** argv)
{
    const auto down =
        renderGesture(fiddle::BowAction::DownBow, 0.82f);
    const auto accent =
        renderGesture(fiddle::BowAction::AccentStroke, 0.82f);
    const auto shortStroke =
        renderGesture(fiddle::BowAction::ShortStroke, 0.82f);
    const auto chop =
        renderGesture(fiddle::BowAction::Chop, 0.82f);
    const auto tremoloCatch =
        measureReversalCatch(fiddle::BowAction::Tremolo, 0.82f);
    const auto shuffleCatch =
        measureReversalCatch(fiddle::BowAction::Shuffle, 0.82f);
    const auto shortRelease =
        measureOneShotRelease(fiddle::BowAction::ShortStroke, 0.82f);
    const auto accentRelease =
        measureOneShotRelease(fiddle::BowAction::AccentStroke, 0.82f);

    std::cout << "down_onset_ms=" << down.onsetMs
              << " down_peak_rms=" << down.peakRms << '\n'
              << "accent_onset_ms=" << accent.onsetMs
              << " accent_peak_rms=" << accent.peakRms
              << " accent_early_rms=" << accent.earlyRms << '\n'
              << "short_onset_ms=" << shortStroke.onsetMs
              << " short_peak_rms=" << shortStroke.peakRms
              << " short_early_rms=" << shortStroke.earlyRms << '\n'
              << "chop_onset_ms=" << chop.onsetMs
              << " chop_peak_rms=" << chop.peakRms << '\n'
              << "tremolo_reversal_max_ms=" << tremoloCatch.maxLatencyMs
              << " tremolo_reversal_mean_ms=" << tremoloCatch.meanLatencyMs
              << " tremolo_interval_ratio=" << tremoloCatch.intervalRatio
              << " tremolo_reversals=" << tremoloCatch.reversals << '\n'
              << "shuffle_reversal_max_ms=" << shuffleCatch.maxLatencyMs
              << " shuffle_reversal_mean_ms=" << shuffleCatch.meanLatencyMs
              << " shuffle_interval_ratio=" << shuffleCatch.intervalRatio
              << " shuffle_reversals=" << shuffleCatch.reversals << '\n'
              << "short_release_half_ms=" << shortRelease.halfLiftMs
              << " short_release_10pct_ms=" << shortRelease.tenPercentLiftMs
              << " short_release_end_ms=" << shortRelease.endLiftMs << '\n'
              << "accent_release_half_ms=" << accentRelease.halfLiftMs
              << " accent_release_10pct_ms=" << accentRelease.tenPercentLiftMs
              << " accent_release_end_ms=" << accentRelease.endLiftMs << '\n';

    if (!(std::isfinite(down.onsetMs)
          && std::isfinite(accent.onsetMs)
          && down.peakRms > 1.0e-5
          && accent.peakRms > 1.0e-5))
        return fail("gesture onset render was silent or non-finite");

    if (!(accent.onsetMs + 1.0 < down.onsetMs))
        return fail("Accent did not catch the string earlier than ordinary Down Bow");

    if (!(shortStroke.onsetMs + 4.0 < down.onsetMs))
        return fail("Short Stroke did not catch the string clearly earlier than ordinary Down Bow");

    if (!(accent.earlyRms >= 3.0 * shortStroke.earlyRms))
        return fail("Accent did not produce a clearly stronger first-20-ms bow catch than Short Stroke");

    if (!(chop.peakRms > 1.0e-5
          && chop.onsetMs + 2.0 < accent.onsetMs
          && chop.earlyRms >= 1.5 * accent.earlyRms))
        return fail("Chop did not produce a distinct collision-dominant contact transient");

    if (!(tremoloCatch.reversals >= 4
          && tremoloCatch.maxLatencyMs <= 16.0))
        return fail("Tremolo bow reversal did not re-catch useful speed promptly");

    if (!(shuffleCatch.reversals >= 4
          && shuffleCatch.maxLatencyMs <= 18.0))
        return fail("Shuffle bow reversal did not re-catch useful speed promptly");

    if (!(tremoloCatch.intervalRatio <= 1.10
          && shuffleCatch.intervalRatio >= 1.65))
        return fail("Tremolo and Shuffle reversal timing is not musically distinct");

    if (!(shortRelease.sawActive && accentRelease.sawActive
          && shortRelease.endedInactive && accentRelease.endedInactive))
        return fail("One-shot bow lifetime flag did not track the physical release");

    if (!(accentRelease.halfLiftMs + 2.0 < shortRelease.halfLiftMs
          && accentRelease.tenPercentLiftMs + 4.0
             < shortRelease.tenPercentLiftMs
          && accentRelease.endLiftMs + 4.0 < shortRelease.endLiftMs))
        return fail("Accent bow lift is not clearly quicker than Short Stroke");

    if (argc >= 2)
    {
        const std::filesystem::path outputDirectory(argv[1]);
        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
            return fail("Could not create gesture onset artifact directory");

        std::ofstream csv(outputDirectory / "gesture_onset_metrics.csv");
        if (!csv)
            return fail("Could not write gesture onset metrics CSV");

        csv << "gesture,onset_ms,peak_rms,early_20ms_rms\n"
            << std::setprecision(9)
            << "Down," << down.onsetMs << ',' << down.peakRms << ','
            << down.earlyRms << '\n'
            << "Short," << shortStroke.onsetMs << ',' << shortStroke.peakRms << ','
            << shortStroke.earlyRms << '\n'
            << "Accent," << accent.onsetMs << ',' << accent.peakRms << ','
            << accent.earlyRms << '\n'
            << "Chop," << chop.onsetMs << ',' << chop.peakRms << ','
            << chop.earlyRms << '\n';

        std::ofstream releaseCsv(
            outputDirectory / "gesture_release_metrics.csv");
        if (!releaseCsv)
            return fail("Could not write gesture release metrics CSV");

        releaseCsv
            << "gesture,half_lift_ms,ten_percent_lift_ms,end_lift_ms,bow_speed_at_end_mps\n"
            << std::setprecision(9)
            << "Short," << shortRelease.halfLiftMs << ','
            << shortRelease.tenPercentLiftMs << ','
            << shortRelease.endLiftMs << ','
            << shortRelease.bowSpeedAtEndMps << '\n'
            << "Accent," << accentRelease.halfLiftMs << ','
            << accentRelease.tenPercentLiftMs << ','
            << accentRelease.endLiftMs << ','
            << accentRelease.bowSpeedAtEndMps << '\n';

        std::ofstream reversalCsv(
            outputDirectory / "gesture_reversal_metrics.csv");
        if (!reversalCsv)
            return fail("Could not write gesture reversal metrics CSV");

        reversalCsv
            << "gesture,reversals,max_latency_ms,mean_latency_ms,min_interval_ms,max_interval_ms,interval_ratio\n"
            << std::setprecision(9)
            << "Tremolo," << tremoloCatch.reversals << ','
            << tremoloCatch.maxLatencyMs << ','
            << tremoloCatch.meanLatencyMs << ','
            << tremoloCatch.minIntervalMs << ','
            << tremoloCatch.maxIntervalMs << ','
            << tremoloCatch.intervalRatio << '\n'
            << "Shuffle," << shuffleCatch.reversals << ','
            << shuffleCatch.maxLatencyMs << ','
            << shuffleCatch.meanLatencyMs << ','
            << shuffleCatch.minIntervalMs << ','
            << shuffleCatch.maxIntervalMs << ','
            << shuffleCatch.intervalRatio << '\n';

        if (!writeComparisonWav(
                outputDirectory / "10_gesture_onset_comparison.wav",
                { down, shortStroke, accent, chop }))
            return fail("Could not write gesture onset comparison WAV");
    }

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
