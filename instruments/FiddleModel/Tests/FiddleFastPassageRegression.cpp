#include "Dsp/FiddleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr double noteSeconds = 0.075;

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

void writeU16(std::ofstream& out, std::uint16_t value)
{
    const char b[2] {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)
    };
    out.write(b, 2);
}

void writeU32(std::ofstream& out, std::uint32_t value)
{
    const char b[4] {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)
    };
    out.write(b, 4);
}

bool writeStereoWav(const std::filesystem::path& path,
                    const std::vector<float>& left,
                    const std::vector<float>& right)
{
    if (left.size() != right.size())
        return false;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;

    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bits = 16;
    const auto frames = static_cast<std::uint32_t>(left.size());
    const auto bytes = frames * channels * (bits / 8u);

    out.write("RIFF", 4); writeU32(out, 36u + bytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4); writeU32(out, 16u);
    writeU16(out, 1u); writeU16(out, channels);
    writeU32(out, static_cast<std::uint32_t>(sampleRate));
    writeU32(out, static_cast<std::uint32_t>(sampleRate) * 4u);
    writeU16(out, 4u); writeU16(out, bits);
    out.write("data", 4); writeU32(out, bytes);

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const auto encode = [](float x)
        {
            constexpr float listeningGain = 3.9810717f; // +12 dB audition level.
            x = std::clamp(x * listeningGain, -1.0f, 1.0f);
            return static_cast<std::uint16_t>(
                static_cast<std::int16_t>(std::lrint(x * 32767.0f)));
        };
        writeU16(out, encode(left[i]));
        writeU16(out, encode(right[i]));
    }
    return static_cast<bool>(out);
}

int fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main(int argc, char** argv)
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
    double earlyRmsSum = 0.0;
    double earlyRmsMin = std::numeric_limits<double>::max();
    double initialEarlyRms = 0.0;
    double reversalEarlyRmsSum = 0.0;
    double reversalEarlyRmsMin = std::numeric_limits<double>::max();
    double reversalCatchSumMs = 0.0;
    double reversalCatchMaxMs = 0.0;
    int measuredReversals = 0;

    constexpr auto earlyWindowSamples =
        static_cast<std::size_t>(0.024 * sampleRate);
    constexpr double usefulReversalSpeedMps = 0.060;

    for (std::size_t noteIndex = 0; noteIndex < phrase.size(); ++noteIndex)
    {
        engine.beginBowStroke(true);
        engine.noteOn(phrase[noteIndex], 0.88f);

        const auto offset = noteIndex * samplesPerNote;
        std::size_t reversalCatchSample = samplesPerNote;

        for (std::size_t sample = 0; sample < samplesPerNote; ++sample)
        {
            engine.process(
                left.data() + offset + sample,
                right.data() + offset + sample,
                1);

            if (noteIndex > 0 && reversalCatchSample == samplesPerNote)
            {
                const auto state = engine.debugSnapshot();
                if (state.bowSpeedMps
                        * static_cast<float>(expectedDirection)
                    >= usefulReversalSpeedMps)
                    reversalCatchSample = sample;
            }
        }

        const auto debug = engine.debugSnapshot();
        if (debug.bowDirection != expectedDirection)
            return fail("alternate bow direction did not toggle on each new note");

        const auto earlyEnd = offset
            + std::min(earlyWindowSamples, samplesPerNote);
        const auto earlySegmentRms = rms(left, offset, earlyEnd);
        earlyRmsSum += earlySegmentRms;
        earlyRmsMin = std::min(earlyRmsMin, earlySegmentRms);

        if (noteIndex == 0)
        {
            initialEarlyRms = earlySegmentRms;
        }
        else
        {
            reversalEarlyRmsSum += earlySegmentRms;
            reversalEarlyRmsMin =
                std::min(reversalEarlyRmsMin, earlySegmentRms);

            const auto latencyMs =
                reversalCatchSample < samplesPerNote
                    ? 1000.0
                        * static_cast<double>(reversalCatchSample + 1)
                        / sampleRate
                    : noteSeconds * 1000.0;
            reversalCatchSumMs += latencyMs;
            reversalCatchMaxMs = std::max(reversalCatchMaxMs, latencyMs);
            ++measuredReversals;
        }

        expectedDirection = -expectedDirection;

        // Keep the existing sustained-note guard while separately measuring
        // the formerly ignored first 24 ms above.
        const auto segmentBegin = offset + earlyWindowSamples;
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

    const auto earlyRmsMean =
        earlyRmsSum / static_cast<double>(phrase.size());
    const auto reversalEarlyRmsMean =
        measuredReversals > 0
            ? reversalEarlyRmsSum
                / static_cast<double>(measuredReversals)
            : 0.0;
    const auto reversalCatchMeanMs =
        measuredReversals > 0
            ? reversalCatchSumMs / static_cast<double>(measuredReversals)
            : 0.0;

    if (initialEarlyRms < 8.0e-4)
        return fail("first bow stroke is too weak during its first 24 ms");

    if (reversalEarlyRmsMin < 6.0e-3)
        return fail("alternate bow reversal has a weak first-24-ms transient");

    if (reversalCatchMaxMs > 8.0)
        return fail("alternate bow reversal did not re-catch useful speed within 8 ms");

    for (const auto sample : left)
        if (!std::isfinite(sample) || std::abs(sample) > 8.0f)
            return fail("fast passage produced non-finite or runaway audio");

    engine.noteOff();

    if (argc >= 2)
    {
        const std::filesystem::path wavPath(argv[1]);
        if (!writeStereoWav(wavPath, left, right))
            return fail("could not write fast-passage listening WAV");

        const auto metricsPath =
            wavPath.parent_path() / "fast_passage_metrics.csv";
        std::ofstream metrics(metricsPath);
        if (!metrics)
            return fail("could not write fast-passage metrics CSV");

        metrics
            << "metric,value\n"
            << std::setprecision(9)
            << "early_24ms_rms_min," << earlyRmsMin << '\n'
            << "early_24ms_rms_mean," << earlyRmsMean << '\n'
            << "initial_24ms_rms," << initialEarlyRms << '\n'
            << "reversal_24ms_rms_min," << reversalEarlyRmsMin << '\n'
            << "reversal_24ms_rms_mean," << reversalEarlyRmsMean << '\n'
            << "reversal_catch_max_ms," << reversalCatchMaxMs << '\n'
            << "reversal_catch_mean_ms," << reversalCatchMeanMs << '\n';

        std::cout << "wav=" << wavPath.string() << '\n'
                  << "fast_passage_metrics="
                  << metricsPath.string() << '\n';
    }

    std::cout << "PASS\n"
              << "notes=" << phrase.size() << '\n'
              << "note_ms=" << noteSeconds * 1000.0 << '\n'
              << "early_24ms_rms_min=" << earlyRmsMin << '\n'
              << "early_24ms_rms_mean=" << earlyRmsMean << '\n'
              << "initial_24ms_rms=" << initialEarlyRms << '\n'
              << "reversal_24ms_rms_min=" << reversalEarlyRmsMin << '\n'
              << "reversal_24ms_rms_mean=" << reversalEarlyRmsMean << '\n'
              << "reversal_catch_max_ms=" << reversalCatchMaxMs << '\n'
              << "reversal_catch_mean_ms=" << reversalCatchMeanMs << '\n';

    return EXIT_SUCCESS;
}
