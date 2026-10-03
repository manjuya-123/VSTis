#include "Dsp/FiddleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

    constexpr float listeningGain = 0.16f;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const auto encode = [](float x)
        {
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

    if (argc >= 2)
    {
        const std::filesystem::path wavPath(argv[1]);
        if (!writeStereoWav(wavPath, left, right))
            return fail("could not write fast-passage listening WAV");
        std::cout << "wav=" << wavPath.string() << '\n';
    }

    std::cout << "PASS\n"
              << "notes=" << phrase.size() << '\n'
              << "note_ms=" << noteSeconds * 1000.0 << '\n';

    return EXIT_SUCCESS;
}
