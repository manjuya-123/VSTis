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
constexpr float listenGain = 0.15f;

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

bool writeWav(const std::filesystem::path& path,
              const std::vector<float>& left,
              const std::vector<float>& right)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary);
    if (!out || left.size() != right.size())
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
            x = std::clamp(x * listenGain, -1.0f, 1.0f);
            return static_cast<std::uint16_t>(
                static_cast<std::int16_t>(std::lrint(x * 32767.0f)));
        };
        writeU16(out, encode(left[i]));
        writeU16(out, encode(right[i]));
    }

    return static_cast<bool>(out);
}

void appendSilence(std::vector<float>& left,
                   std::vector<float>& right,
                   double seconds)
{
    const auto count = static_cast<std::size_t>(seconds * sampleRate);
    left.insert(left.end(), count, 0.0f);
    right.insert(right.end(), count, 0.0f);
}

void render(fiddle::FiddleEngine& engine,
            std::vector<float>& left,
            std::vector<float>& right,
            double seconds)
{
    const auto count = static_cast<std::size_t>(seconds * sampleRate);
    const auto offset = left.size();
    left.resize(offset + count, 0.0f);
    right.resize(offset + count, 0.0f);
    engine.process(left.data() + offset, right.data() + offset, count);
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
    controls.speed = 0.66f;
    controls.attack = 0.78f;
    controls.position = 0.48f;
    controls.balance = -0.45f;
    controls.vibratoWidth = 0.0f;
    engine.setControls(controls);

    // Physical left-hand shape: E4 on D string + B4 on A string.
    std::array<float, 4> fingering {};
    fingering[1] = 329.6276f;
    fingering[2] = 493.8833f;
    engine.setFingeringLayout(fingering, 1, 1, 0.88f);

    std::vector<float> left;
    std::vector<float> right;

    // Down bow.
    engine.startBow(+1);
    render(engine, left, right, 0.28);
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Up bow, same left hand.
    engine.startBow(-1);
    render(engine, left, right, 0.28);
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Short stroke.
    engine.startShortStroke(+1, 0.075f);
    render(engine, left, right, 0.18);
    render(engine, left, right, 0.06);

    // Tremolo on the same stopped notes.
    engine.startTremolo(14.0f);
    render(engine, left, right, 0.52);
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Drone/double-stop gesture: same fingering, even focus across D/A.
    controls.balance = 0.0f;
    engine.setControls(controls);
    engine.startBow(+1);
    render(engine, left, right, 0.38);
    engine.stopBow();
    render(engine, left, right, 0.18);

    for (const auto sample : left)
        if (!std::isfinite(sample) || std::abs(sample) > 8.0f)
            return fail("Fiddle Play demo produced non-finite or runaway audio");

    const auto debug = engine.debugSnapshot();
    if (debug.bowPairLowerString != 1)
        return fail("Fiddle Play demo lost the D/A bow pair");

    if (std::abs(debug.speakingFrequencyHz[1] - 329.6276f) > 1.5f
        || std::abs(debug.speakingFrequencyHz[2] - 493.8833f) > 1.5f)
        return fail("Fiddle Play demo lost the held double-stop fingering");

    if (argc >= 2)
    {
        const std::filesystem::path output(argv[1]);
        if (!writeWav(output, left, right))
            return fail("Could not write Fiddle Play demo WAV");
        std::cout << "wav=" << output.string() << '\n';
    }

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
