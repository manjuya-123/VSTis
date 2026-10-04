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
#include <string>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr double sustainSeconds = 1.8;
constexpr double releaseSeconds = 0.7;
constexpr float listeningGain = 7.9432823f; // +18 dB, matches plugin default Output Level.

struct Scenario
{
    std::string name;
    fiddle::Controls controls;
    float noteHz = 329.6276f;
    float velocity = 0.85f;
    fiddle::MaterialSettings materials{};
};

struct Render
{
    std::vector<float> left;
    std::vector<float> right;
};

struct Metrics
{
    double sustainRms = 0.0;
    double tailRms = 0.0;
    double peak = 0.0;
    double periodicity = 0.0;
    double spectralCentroidHz = 0.0;
    double highBandRatio = 0.0;
    double stereoSideRatio = 0.0;
    bool finite = true;
};

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

bool writeStereoWav16(const std::filesystem::path& path,
                      const std::vector<float>& left,
                      const std::vector<float>& right,
                      float gain = listeningGain)
{
    if (left.size() != right.size())
        return false;

    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;

    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bitsPerSample = 16;
    const auto frames = static_cast<std::uint32_t>(left.size());
    const auto dataBytes = frames * channels * (bitsPerSample / 8u);
    const auto byteRate = static_cast<std::uint32_t>(sampleRate) * channels * (bitsPerSample / 8u);
    const auto blockAlign = static_cast<std::uint16_t>(channels * (bitsPerSample / 8u));

    out.write("RIFF", 4);
    writeU32(out, 36u + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(out, 16u);
    writeU16(out, 1u);
    writeU16(out, channels);
    writeU32(out, static_cast<std::uint32_t>(sampleRate));
    writeU32(out, byteRate);
    writeU16(out, blockAlign);
    writeU16(out, bitsPerSample);
    out.write("data", 4);
    writeU32(out, dataBytes);

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const auto encode = [gain](float value)
        {
            const auto x = std::clamp(value * gain, -1.0f, 1.0f);
            return static_cast<std::int16_t>(std::lrint(x * 32767.0f));
        };

        writeU16(out, static_cast<std::uint16_t>(encode(left[i])));
        writeU16(out, static_cast<std::uint16_t>(encode(right[i])));
    }

    return static_cast<bool>(out);
}

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

double periodicityAtFrequency(const std::vector<float>& x,
                              std::size_t begin,
                              std::size_t end,
                              double frequency)
{
    begin = std::min(begin, x.size());
    end = std::min(end, x.size());
    if (frequency <= 0.0 || end <= begin + 100)
        return 0.0;

    const auto lag = sampleRate / frequency;
    const auto lagInt = static_cast<std::size_t>(std::floor(lag));
    const auto frac = lag - static_cast<double>(lagInt);
    if (begin + lagInt + 2 >= end)
        return 0.0;

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

std::vector<double> makeHannSegment(const std::vector<float>& x,
                                    std::size_t begin,
                                    std::size_t length)
{
    if (begin >= x.size())
        return {};

    length = std::min(length, x.size() - begin);
    if (length < 16)
        return {};

    double mean = 0.0;
    for (std::size_t i = 0; i < length; ++i)
        mean += x[begin + i];
    mean /= static_cast<double>(length);

    std::vector<double> result(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(
            2.0 * 3.14159265358979323846 * static_cast<double>(i)
            / static_cast<double>(length - 1));
        result[i] = (static_cast<double>(x[begin + i]) - mean) * window;
    }
    return result;
}

double goertzelPower(const std::vector<double>& x, double frequency)
{
    if (x.empty())
        return 0.0;

    const auto omega = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
    const auto coeff = 2.0 * std::cos(omega);
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;

    for (const auto sample : x)
    {
        s0 = sample + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

Metrics measure(const Render& render, double fundamentalHz)
{
    Metrics m;
    const auto& x = render.left;

    const auto sustainBegin = static_cast<std::size_t>(0.85 * sampleRate);
    const auto sustainEnd = static_cast<std::size_t>(1.55 * sampleRate);
    const auto tailBegin = static_cast<std::size_t>((sustainSeconds + 0.42) * sampleRate);
    const auto tailEnd = x.size();

    m.sustainRms = rms(x, sustainBegin, sustainEnd);
    m.tailRms = rms(x, tailBegin, tailEnd);
    m.periodicity = periodicityAtFrequency(x, sustainBegin, sustainEnd, fundamentalHz);

    double midEnergy = 0.0;
    double sideEnergy = 0.0;
    for (std::size_t i = sustainBegin; i < sustainEnd; ++i)
    {
        const auto mid = 0.5 * (
            static_cast<double>(render.left[i])
            + static_cast<double>(render.right[i]));
        const auto side = 0.5 * (
            static_cast<double>(render.left[i])
            - static_cast<double>(render.right[i]));
        midEnergy += mid * mid;
        sideEnergy += side * side;
    }
    m.stereoSideRatio =
        std::sqrt(sideEnergy / (midEnergy + 1.0e-30));

    for (const auto value : x)
    {
        m.finite = m.finite && std::isfinite(value);
        m.peak = std::max(m.peak, std::abs(static_cast<double>(value)));
    }

    // Analyse exact DFT-bin frequencies of a 4096-sample Hann window. This
    // catches the string's narrow harmonic lines without requiring an FFT
    // dependency and keeps the metric comparable with an ordinary spectrum.
    constexpr std::size_t spectralLength = 4096;
    const auto spectralBegin = static_cast<std::size_t>(1.00 * sampleRate);
    const auto spectralSegment = makeHannSegment(x, spectralBegin, spectralLength);

    double totalEnergy = 0.0;
    double highEnergy = 0.0;
    double weightedFrequency = 0.0;

    const auto firstBin = static_cast<int>(std::ceil(
        100.0 * static_cast<double>(spectralLength) / sampleRate));
    const auto lastBin = static_cast<int>(std::floor(
        8000.0 * static_cast<double>(spectralLength) / sampleRate));

    for (int bin = firstBin; bin <= lastBin; ++bin)
    {
        const auto frequency =
            static_cast<double>(bin) * sampleRate / static_cast<double>(spectralLength);
        const auto power = goertzelPower(spectralSegment, frequency);
        totalEnergy += power;
        weightedFrequency += frequency * power;
        if (frequency >= 2500.0)
            highEnergy += power;
    }

    m.spectralCentroidHz = totalEnergy > 0.0 ? weightedFrequency / totalEnergy : 0.0;
    m.highBandRatio = totalEnergy > 0.0 ? highEnergy / totalEnergy : 0.0;
    return m;
}

Render renderScenario(const Scenario& scenario)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);
    engine.setMaterials(scenario.materials);
    engine.setControls(scenario.controls);
    engine.noteOn(scenario.noteHz, scenario.velocity);

    const auto sustainSamples = static_cast<std::size_t>(sustainSeconds * sampleRate);
    const auto releaseSamples = static_cast<std::size_t>(releaseSeconds * sampleRate);

    Render result;
    result.left.assign(sustainSamples + releaseSamples, 0.0f);
    result.right.assign(result.left.size(), 0.0f);

    engine.process(result.left.data(), result.right.data(), sustainSamples);
    engine.noteOff();
    engine.process(result.left.data() + sustainSamples,
                   result.right.data() + sustainSamples,
                   releaseSamples);
    return result;
}

Render renderFastAlternatePassage()
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.56f;
    controls.speed = 0.68f;
    controls.attack = 0.82f;
    controls.position = 0.48f;
    controls.balance = -0.82f;
    controls.vibratoWidth = 0.0f;
    controls.vibratoPace = 0.5f;
    engine.setControls(controls);

    constexpr std::array<float, 16> phrase {
        329.6276f, 369.9944f, 391.9954f, 369.9944f,
        329.6276f, 369.9944f, 391.9954f, 440.0000f,
        493.8833f, 440.0000f, 391.9954f, 369.9944f,
        329.6276f, 391.9954f, 369.9944f, 329.6276f
    };

    constexpr double noteSeconds = 0.090;
    const auto samplesPerNote = static_cast<std::size_t>(noteSeconds * sampleRate);
    const auto releaseSamples = static_cast<std::size_t>(0.35 * sampleRate);

    Render result;
    result.left.assign(samplesPerNote * phrase.size() + releaseSamples, 0.0f);
    result.right.assign(result.left.size(), 0.0f);

    for (std::size_t i = 0; i < phrase.size(); ++i)
    {
        engine.beginBowStroke(true);
        engine.noteOn(phrase[i], 0.88f);
        const auto offset = i * samplesPerNote;
        engine.process(result.left.data() + offset,
                       result.right.data() + offset,
                       samplesPerNote);
    }

    engine.noteOff();
    const auto releaseOffset = samplesPerNote * phrase.size();
    engine.process(result.left.data() + releaseOffset,
                   result.right.data() + releaseOffset,
                   releaseSamples);
    return result;
}

fiddle::Controls baseControls()
{
    fiddle::Controls c;
    c.pressure = 0.55f;
    c.speed = 0.60f;
    c.attack = 0.55f;
    c.position = 0.45f;
    c.balance = -0.85f;
    c.vibratoWidth = 0.0f;
    c.vibratoPace = 0.5f;
    return c;
}

std::vector<Scenario> makeScenarios()
{
    std::vector<Scenario> scenarios;

    {
        auto c = baseControls();
        scenarios.push_back({ "01_reference_E4_on_D", c, 329.6276f, 0.85f });
    }
    {
        auto c = baseControls();
        c.position = 0.10f;
        scenarios.push_back({ "02_bow_contact_fingerboard", c, 329.6276f, 0.85f });
    }
    {
        auto c = baseControls();
        c.position = 0.90f;
        scenarios.push_back({ "03_bow_contact_bridge", c, 329.6276f, 0.85f });
    }
    {
        auto c = baseControls();
        c.pressure = 0.25f;
        scenarios.push_back({ "04_bow_pressure_light", c, 329.6276f, 0.85f });
    }
    {
        auto c = baseControls();
        c.pressure = 0.78f;
        scenarios.push_back({ "05_bow_pressure_firm", c, 329.6276f, 0.85f });
    }
    {
        auto c = baseControls();
        c.balance = 0.0f;
        scenarios.push_back({ "06_D_A_double_stop", c, 329.6276f, 0.85f });
    }
    {
        auto c = baseControls();
        c.balance = -0.90f;
        c.vibratoWidth = 0.72f;
        c.vibratoPace = 0.55f;
        scenarios.push_back({ "07_stopped_vibrato_B4_on_A", c, 493.8833f, 0.85f });
    }
    {
        auto c = baseControls();
        fiddle::MaterialSettings m;
        m.body = fiddle::BodyMaterialPreset::LightStiffComposite;
        scenarios.push_back({ "09_body_light_stiff_composite", c, 329.6276f, 0.85f, m });
    }
    {
        auto c = baseControls();
        fiddle::MaterialSettings m;
        m.body = fiddle::BodyMaterialPreset::RigidComposite;
        scenarios.push_back({ "10_body_rigid_composite", c, 329.6276f, 0.85f, m });
    }
    {
        auto c = baseControls();
        fiddle::MaterialSettings m;
        m.contact = fiddle::ContactMaterialPreset::DryLightGrip;
        scenarios.push_back({ "11_hair_rosin_dry_light_grip", c, 329.6276f, 0.85f, m });
    }
    {
        auto c = baseControls();
        fiddle::MaterialSettings m;
        m.contact = fiddle::ContactMaterialPreset::HighGripRosin;
        scenarios.push_back({ "12_hair_rosin_high_grip", c, 329.6276f, 0.85f, m });
    }

    return scenarios;
}

bool passesSanity(const Scenario& scenario, const Metrics& metrics)
{
    const bool finiteAndBounded = metrics.finite && metrics.peak < 8.0;
    const bool audible = metrics.sustainRms > 1.0e-5;
    const bool releases = metrics.tailRms < metrics.sustainRms * 0.85 + 1.0e-8;
    const bool auditionHeadroom =
        metrics.peak * static_cast<double>(listeningGain) < 0.95;
    const bool naturalStereo =
        metrics.stereoSideRatio >= 0.006
        && metrics.stereoSideRatio <= 0.050;

    if (!(finiteAndBounded && audible && releases
          && auditionHeadroom && naturalStereo))
    {
        std::cerr << "FAIL " << scenario.name
                  << " finite=" << metrics.finite
                  << " peak=" << metrics.peak
                  << " sustain_rms=" << metrics.sustainRms
                  << " tail_rms=" << metrics.tailRms
                  << " audition_peak="
                  << metrics.peak * static_cast<double>(listeningGain)
                  << " side_ratio=" << metrics.stereoSideRatio
                  << '\n';
        return false;
    }

    return true;
}
} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path outputDirectory =
        argc >= 2 ? std::filesystem::path(argv[1])
                  : std::filesystem::path("regression-audio");

    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);
    if (ec)
    {
        std::cerr << "FAIL: cannot create output directory: "
                  << outputDirectory.string() << '\n';
        return EXIT_FAILURE;
    }

    const auto scenarios = makeScenarios();
    std::ofstream csv(outputDirectory / "metrics.csv");
    if (!csv)
    {
        std::cerr << "FAIL: cannot create metrics.csv\n";
        return EXIT_FAILURE;
    }

    csv << "scenario,sustain_rms,tail_rms,peak,periodicity_at_note,"
           "spectral_centroid_hz,high_band_ratio,stereo_side_ratio\n";
    csv << std::setprecision(9);

    std::vector<float> comparisonLeft;
    std::vector<float> comparisonRight;
    std::vector<float> rosinShowcaseLeft;
    std::vector<float> rosinShowcaseRight;
    const auto silenceSamples = static_cast<std::size_t>(0.25 * sampleRate);
    bool ok = true;
    std::vector<Metrics> measured;
    measured.reserve(scenarios.size());

    for (const auto& scenario : scenarios)
    {
        auto render = renderScenario(scenario);
        const auto metrics = measure(render, scenario.noteHz);

        csv << scenario.name << ','
            << metrics.sustainRms << ','
            << metrics.tailRms << ','
            << metrics.peak << ','
            << metrics.periodicity << ','
            << metrics.spectralCentroidHz << ','
            << metrics.highBandRatio << ','
            << metrics.stereoSideRatio << '\n';

        std::cout << scenario.name
                  << " rms=" << metrics.sustainRms
                  << " tail=" << metrics.tailRms
                  << " peak=" << metrics.peak
                  << " periodicity=" << metrics.periodicity
                  << " centroid_hz=" << metrics.spectralCentroidHz
                  << " hf_ratio=" << metrics.highBandRatio
                  << " side_ratio=" << metrics.stereoSideRatio << '\n';

        ok = passesSanity(scenario, metrics) && ok;
        measured.push_back(metrics);

        if (!writeStereoWav16(outputDirectory / (scenario.name + ".wav"),
                              render.left, render.right))
        {
            std::cerr << "FAIL: cannot write " << scenario.name << ".wav\n";
            ok = false;
        }

        comparisonLeft.insert(comparisonLeft.end(), render.left.begin(), render.left.end());
        comparisonRight.insert(comparisonRight.end(), render.right.begin(), render.right.end());
        comparisonLeft.insert(comparisonLeft.end(), silenceSamples, 0.0f);
        comparisonRight.insert(comparisonRight.end(), silenceSamples, 0.0f);

        if (scenario.name == "01_reference_E4_on_D"
            || scenario.name == "11_hair_rosin_dry_light_grip"
            || scenario.name == "12_hair_rosin_high_grip")
        {
            rosinShowcaseLeft.insert(
                rosinShowcaseLeft.end(), render.left.begin(), render.left.end());
            rosinShowcaseRight.insert(
                rosinShowcaseRight.end(), render.right.begin(), render.right.end());
            rosinShowcaseLeft.insert(
                rosinShowcaseLeft.end(), silenceSamples, 0.0f);
            rosinShowcaseRight.insert(
                rosinShowcaseRight.end(), silenceSamples, 0.0f);
        }
    }

    if (!writeStereoWav16(
            outputDirectory / "12_rosin_texture_showcase.wav",
            rosinShowcaseLeft,
            rosinShowcaseRight))
    {
        std::cerr << "FAIL: cannot write rosin texture showcase WAV\n";
        ok = false;
    }

    const auto fastPassage = renderFastAlternatePassage();
    if (!writeStereoWav16(outputDirectory / "08_fast_alternate_passage.wav",
                          fastPassage.left, fastPassage.right))
    {
        std::cerr << "FAIL: cannot write fast passage WAV\n";
        ok = false;
    }

    comparisonLeft.insert(comparisonLeft.end(),
                          fastPassage.left.begin(), fastPassage.left.end());
    comparisonRight.insert(comparisonRight.end(),
                           fastPassage.right.begin(), fastPassage.right.end());

    if (!writeStereoWav16(outputDirectory / "00_comparison.wav",
                          comparisonLeft, comparisonRight))
    {
        std::cerr << "FAIL: cannot write comparison WAV\n";
        ok = false;
    }

    // The normal UI explicitly promises that moving Bow Contact toward the
    // bridge makes the tone brighter. Keep that player-facing cause/effect
    // true even while the internal body/bow model evolves.
    if (measured.size() >= 3)
    {
        const auto& fingerboard = measured[1];
        const auto& bridge = measured[2];
        // Spectral centroid is the primary perceptual-brightness guard.
        // The >2.5 kHz ratio is secondary: individual narrow harmonics can move
        // across that fixed boundary even while the overall spectrum gets brighter.
        const bool brighterAtBridge =
            bridge.spectralCentroidHz > fingerboard.spectralCentroidHz * 1.10
            && bridge.highBandRatio > fingerboard.highBandRatio * 0.70;

        if (!brighterAtBridge)
        {
            std::cerr << "FAIL: Bow Contact no longer gets audibly brighter toward bridge"
                      << " fingerboard_centroid=" << fingerboard.spectralCentroidHz
                      << " bridge_centroid=" << bridge.spectralCentroidHz
                      << " fingerboard_hf=" << fingerboard.highBandRatio
                      << " bridge_hf=" << bridge.highBandRatio << '\n';
            ok = false;
        }
    }

    if (!ok)
        return EXIT_FAILURE;

    std::cout << "PASS audio regression; artifacts=" << outputDirectory.string() << '\n';
    return EXIT_SUCCESS;
}
