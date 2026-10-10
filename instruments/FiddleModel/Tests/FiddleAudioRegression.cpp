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
    double adjacentCycleDifferenceRatio = 0.0;
    double spectralCentroidHz = 0.0;
    double highBandRatio = 0.0;
    double harmonicEnvelopeIrregularityDb = 0.0;
    double stereoSideRatio = 0.0;
    double stereoLowBandSideRatio = 0.0;
    double stereoHighBandSideRatio = 0.0;
    bool finite = true;
};

struct CycleCorrelationStats
{
    double mean = 0.0;
    double standardDeviation = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    std::size_t pairCount = 0;
};

struct TransitionIntervalStats
{
    double meanSamples = 0.0;
    double standardDeviationSamples = 0.0;
    double minimumSamples = 0.0;
    double maximumSamples = 0.0;
    std::size_t intervalCount = 0;
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

double differenceRms(const std::vector<float>& a,
                     const std::vector<float>& b,
                     std::size_t begin,
                     std::size_t end)
{
    end = std::min({ end, a.size(), b.size() });
    begin = std::min(begin, end);
    if (end <= begin)
        return 0.0;

    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
    {
        const auto d =
            static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += d * d;
    }
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

double cycleDifferenceRatio(const std::vector<float>& x,
                            std::size_t begin,
                            std::size_t end,
                            double frequency,
                            int separationCycles = 1)
{
    begin = std::min(begin, x.size());
    end = std::min(end, x.size());
    if (frequency <= 0.0 || end <= begin + 100)
        return 0.0;

    separationCycles = std::max(1, separationCycles);
    const auto lag =
        static_cast<double>(separationCycles) * sampleRate / frequency;
    const auto lagInt = static_cast<std::size_t>(std::floor(lag));
    const auto frac = lag - static_cast<double>(lagInt);
    if (begin + lagInt + 2 >= end)
        return 0.0;

    double signalSq = 0.0;
    double differenceSq = 0.0;
    std::size_t count = 0;
    const auto last = end - lagInt - 1;
    for (std::size_t i = begin; i < last; ++i)
    {
        const auto a = static_cast<double>(x[i]);
        const auto d0 = static_cast<double>(x[i + lagInt]);
        const auto d1 = static_cast<double>(x[i + lagInt + 1]);
        const auto b = (1.0 - frac) * d0 + frac * d1;
        const auto d = b - a;
        signalSq += 0.5 * (a * a + b * b);
        differenceSq += d * d;
        ++count;
    }
    if (count == 0)
        return 0.0;
    return std::sqrt(differenceSq / (signalSq + 1.0e-30));
}

double interpolatedSample(const std::vector<float>& x, double index)
{
    if (x.empty())
        return 0.0;

    index = std::clamp(
        index, 0.0, static_cast<double>(x.size() - 1));
    const auto i0 = static_cast<std::size_t>(std::floor(index));
    const auto i1 = std::min(i0 + 1, x.size() - 1);
    const auto frac = index - static_cast<double>(i0);
    return (1.0 - frac) * static_cast<double>(x[i0])
        + frac * static_cast<double>(x[i1]);
}

CycleCorrelationStats cycleCorrelationStats(
    const std::vector<float>& x,
    std::size_t begin,
    std::size_t end,
    double frequency,
    int separationCycles)
{
    CycleCorrelationStats result;
    begin = std::min(begin, x.size());
    end = std::min(end, x.size());

    if (frequency <= 0.0 || end <= begin)
        return result;

    const auto period = sampleRate / frequency;
    separationCycles = std::max(1, separationCycles);
    const auto separation =
        static_cast<double>(separationCycles) * period;
    if (period < 4.0
        || static_cast<double>(end - begin) < separation + 2.0 * period)
        return result;

    constexpr int phaseSamples = 96;
    std::vector<double> correlations;
    for (double cycleStart = static_cast<double>(begin);
         cycleStart + separation + period < static_cast<double>(end);
         cycleStart += period)
    {
        double meanA = 0.0;
        double meanB = 0.0;
        for (int phase = 0; phase < phaseSamples; ++phase)
        {
            const auto phaseOffset =
                (static_cast<double>(phase) + 0.5)
                * period / static_cast<double>(phaseSamples);
            meanA += interpolatedSample(x, cycleStart + phaseOffset);
            meanB += interpolatedSample(
                x, cycleStart + separation + phaseOffset);
        }
        meanA /= static_cast<double>(phaseSamples);
        meanB /= static_cast<double>(phaseSamples);

        double dot = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        for (int phase = 0; phase < phaseSamples; ++phase)
        {
            const auto phaseOffset =
                (static_cast<double>(phase) + 0.5)
                * period / static_cast<double>(phaseSamples);
            const auto a =
                interpolatedSample(x, cycleStart + phaseOffset) - meanA;
            const auto b = interpolatedSample(
                x, cycleStart + separation + phaseOffset) - meanB;
            dot += a * b;
            energyA += a * a;
            energyB += b * b;
        }

        const auto denominator = std::sqrt(energyA * energyB);
        if (denominator > 1.0e-24)
            correlations.push_back(
                std::clamp(dot / denominator, -1.0, 1.0));
    }

    if (correlations.empty())
        return result;

    result.pairCount = correlations.size();
    result.minimum = correlations.front();
    result.maximum = correlations.front();
    double sum = 0.0;
    for (const auto value : correlations)
    {
        sum += value;
        result.minimum = std::min(result.minimum, value);
        result.maximum = std::max(result.maximum, value);
    }
    result.mean = sum / static_cast<double>(correlations.size());

    double variance = 0.0;
    for (const auto value : correlations)
    {
        const auto deviation = value - result.mean;
        variance += deviation * deviation;
    }
    result.standardDeviation = std::sqrt(
        variance / static_cast<double>(correlations.size()));
    return result;
}

TransitionIntervalStats slipOnsetIntervalStats(
    const std::vector<std::uint8_t>& sticking,
    std::size_t begin,
    std::size_t end)
{
    TransitionIntervalStats result;
    begin = std::min(begin, sticking.size());
    end = std::min(end, sticking.size());
    if (end <= begin + 2)
        return result;

    std::vector<double> intervals;
    std::size_t previousOnset = 0;
    bool havePreviousOnset = false;
    for (std::size_t i = begin + 1; i < end; ++i)
    {
        const bool wasSticking = sticking[i - 1] != 0;
        const bool isSticking = sticking[i] != 0;
        if (wasSticking && !isSticking)
        {
            if (havePreviousOnset)
                intervals.push_back(
                    static_cast<double>(i - previousOnset));
            previousOnset = i;
            havePreviousOnset = true;
        }
    }

    if (intervals.empty())
        return result;

    result.intervalCount = intervals.size();
    result.minimumSamples = intervals.front();
    result.maximumSamples = intervals.front();
    double sum = 0.0;
    for (const auto value : intervals)
    {
        sum += value;
        result.minimumSamples =
            std::min(result.minimumSamples, value);
        result.maximumSamples =
            std::max(result.maximumSamples, value);
    }
    result.meanSamples =
        sum / static_cast<double>(intervals.size());

    double variance = 0.0;
    for (const auto value : intervals)
    {
        const auto d = value - result.meanSamples;
        variance += d * d;
    }
    result.standardDeviationSamples = std::sqrt(
        variance / static_cast<double>(intervals.size()));
    return result;
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
    m.adjacentCycleDifferenceRatio = cycleDifferenceRatio(
        x, sustainBegin, sustainEnd, fundamentalHz);

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

    std::vector<float> midSignal(render.left.size(), 0.0f);
    std::vector<float> sideSignal(render.left.size(), 0.0f);
    for (std::size_t i = 0; i < render.left.size(); ++i)
    {
        midSignal[i] = 0.5f * (render.left[i] + render.right[i]);
        sideSignal[i] = 0.5f * (render.left[i] - render.right[i]);
    }
    const auto midSpectralSegment =
        makeHannSegment(midSignal, spectralBegin, spectralLength);
    const auto sideSpectralSegment =
        makeHannSegment(sideSignal, spectralBegin, spectralLength);

    double totalEnergy = 0.0;
    double highEnergy = 0.0;
    double weightedFrequency = 0.0;
    double lowMidEnergy = 0.0;
    double lowSideEnergy = 0.0;
    double highMidEnergy = 0.0;
    double highSideEnergy = 0.0;

    const auto firstBin = static_cast<int>(std::ceil(
        100.0 * static_cast<double>(spectralLength) / sampleRate));
    const auto lastBin = static_cast<int>(std::floor(
        8000.0 * static_cast<double>(spectralLength) / sampleRate));

    for (int bin = firstBin; bin <= lastBin; ++bin)
    {
        const auto frequency =
            static_cast<double>(bin) * sampleRate / static_cast<double>(spectralLength);
        const auto power = goertzelPower(spectralSegment, frequency);
        const auto midPower = goertzelPower(midSpectralSegment, frequency);
        const auto sidePower = goertzelPower(sideSpectralSegment, frequency);
        totalEnergy += power;
        weightedFrequency += frequency * power;
        if (frequency >= 2500.0)
            highEnergy += power;

        if (frequency < 800.0)
        {
            lowMidEnergy += midPower;
            lowSideEnergy += sidePower;
        }
        else if (frequency >= 2500.0)
        {
            highMidEnergy += midPower;
            highSideEnergy += sidePower;
        }
    }

    m.spectralCentroidHz = totalEnergy > 0.0 ? weightedFrequency / totalEnergy : 0.0;
    m.highBandRatio = totalEnergy > 0.0 ? highEnergy / totalEnergy : 0.0;

    // Diagnostic only: an ideal oscillator/sawtooth tends to have a very
    // smooth harmonic envelope. A real violin body/radiativity transfer puts
    // broad peaks, dips and antiresonances across that harmonic comb. Measure
    // the RMS departure (in dB) from the best straight line versus log2 of
    // harmonic number. No acceptance threshold is attached to this yet.
    {
        constexpr int maxHarmonics = 12;
        std::array<double, maxHarmonics> xLog{};
        std::array<double, maxHarmonics> yDb{};
        int count = 0;
        for (int harmonic = 1; harmonic <= maxHarmonics; ++harmonic)
        {
            const auto frequency = fundamentalHz * harmonic;
            if (frequency >= 8000.0 || frequency >= 0.48 * sampleRate)
                break;
            const auto power = std::max(
                1.0e-30, goertzelPower(spectralSegment, frequency));
            xLog[static_cast<std::size_t>(count)] =
                std::log2(static_cast<double>(harmonic));
            yDb[static_cast<std::size_t>(count)] = 10.0 * std::log10(power);
            ++count;
        }
        if (count >= 4)
        {
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const auto xx = xLog[static_cast<std::size_t>(i)];
                const auto yy = yDb[static_cast<std::size_t>(i)];
                sx += xx;
                sy += yy;
                sxx += xx * xx;
                sxy += xx * yy;
            }
            const auto denom = count * sxx - sx * sx;
            const auto slope = std::abs(denom) > 1.0e-20
                ? (count * sxy - sx * sy) / denom
                : 0.0;
            const auto intercept = (sy - slope * sx) / count;
            double residual = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const auto error = yDb[static_cast<std::size_t>(i)]
                    - (intercept + slope * xLog[static_cast<std::size_t>(i)]);
                residual += error * error;
            }
            m.harmonicEnvelopeIrregularityDb =
                std::sqrt(residual / static_cast<double>(count));
        }
    }
    m.stereoLowBandSideRatio =
        std::sqrt(lowSideEnergy / (lowMidEnergy + 1.0e-30));
    m.stereoHighBandSideRatio =
        std::sqrt(highSideEnergy / (highMidEnergy + 1.0e-30));
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

struct CycleTrace
{
    std::vector<std::uint8_t> sticking;
    std::vector<std::uint8_t> staticFallback;
    std::vector<float> contactFrictionForce;
    std::vector<float> contactGripUtilization;
    std::vector<float> torsionalSurfaceVelocity;
    std::vector<float> bowInjectionVelocity;
    std::vector<float> incidentBridgeVelocity;
    std::vector<float> bridgeVelocity;
    std::vector<float> radiated;
};

CycleTrace renderCycleTrace(int stringIndex,
                            int pairLower,
                            float balance,
                            float targetHz)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    auto controls = baseControls();
    controls.balance = balance;
    controls.singleStringIsolation = 1.0f;
    engine.setControls(controls);

    std::array<float, 4> layout {};
    layout[static_cast<std::size_t>(stringIndex)] = targetHz;
    engine.setFingeringLayout(
        layout, stringIndex, pairLower, 0.85f);
    engine.startBow(+1);

    const auto samples =
        static_cast<std::size_t>(sustainSeconds * sampleRate);
    CycleTrace trace;
    trace.sticking.reserve(samples);
    trace.staticFallback.reserve(samples);
    trace.contactFrictionForce.reserve(samples);
    trace.contactGripUtilization.reserve(samples);
    trace.torsionalSurfaceVelocity.reserve(samples);
    trace.bowInjectionVelocity.reserve(samples);
    trace.incidentBridgeVelocity.reserve(samples);
    trace.bridgeVelocity.reserve(samples);
    trace.radiated.reserve(samples);

    for (std::size_t sample = 0; sample < samples; ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);
        const auto state = engine.debugSnapshot();
        const auto index = static_cast<std::size_t>(stringIndex);
        trace.sticking.push_back(
            state.sticking[index] ? std::uint8_t{1} : std::uint8_t{0});
        trace.staticFallback.push_back(
            state.contactStaticFallback[index] ? std::uint8_t{1} : std::uint8_t{0});
        trace.contactFrictionForce.push_back(
            state.contactFrictionForceN[index]);
        trace.contactGripUtilization.push_back(
            state.contactGripUtilization[index]);
        trace.torsionalSurfaceVelocity.push_back(
            state.torsionalSurfaceVelocityMps[index]);
        trace.bowInjectionVelocity.push_back(
            state.bowInjectionVelocityMps[index]);
        trace.incidentBridgeVelocity.push_back(
            state.incidentBridgeVelocityMps[index]);
        trace.bridgeVelocity.push_back(state.bridgeVelocity);
        trace.radiated.push_back(left);
    }
    return trace;
}

Render renderSamePitchOnString(int stringIndex,
                              int pairLower,
                              float balance,
                              float targetHz,
                              bool singleStringIsolation = false)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    auto controls = baseControls();
    controls.balance = balance;
    controls.singleStringIsolation = singleStringIsolation ? 1.0f : 0.0f;
    engine.setControls(controls);

    std::array<float, 4> layout {};
    layout[static_cast<std::size_t>(stringIndex)] = targetHz;
    engine.setFingeringLayout(
        layout,
        stringIndex,
        pairLower,
        0.85f);
    engine.startBow(+1);

    const auto sustainSamples =
        static_cast<std::size_t>(sustainSeconds * sampleRate);
    const auto releaseSamples =
        static_cast<std::size_t>(releaseSeconds * sampleRate);

    Render result;
    result.left.assign(sustainSamples + releaseSamples, 0.0f);
    result.right.assign(result.left.size(), 0.0f);

    engine.process(
        result.left.data(), result.right.data(), sustainSamples);
    engine.noteOff();
    engine.process(
        result.left.data() + sustainSamples,
        result.right.data() + sustainSamples,
        releaseSamples);
    return result;
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
    const bool frequencyDependentStereo =
        metrics.stereoLowBandSideRatio >= 0.004
        && metrics.stereoLowBandSideRatio <= 0.035
        && metrics.stereoHighBandSideRatio >= 0.012
        && metrics.stereoHighBandSideRatio <= 0.060
        && metrics.stereoHighBandSideRatio
            >= metrics.stereoLowBandSideRatio * 1.40;

    if (!(finiteAndBounded && audible && releases
          && auditionHeadroom && naturalStereo
          && frequencyDependentStereo))
    {
        std::cerr << "FAIL " << scenario.name
                  << " finite=" << metrics.finite
                  << " peak=" << metrics.peak
                  << " sustain_rms=" << metrics.sustainRms
                  << " tail_rms=" << metrics.tailRms
                  << " audition_peak="
                  << metrics.peak * static_cast<double>(listeningGain)
                  << " side_ratio=" << metrics.stereoSideRatio
                  << " low_side_ratio=" << metrics.stereoLowBandSideRatio
                  << " high_side_ratio=" << metrics.stereoHighBandSideRatio
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
           "spectral_centroid_hz,high_band_ratio,stereo_side_ratio,"
           "stereo_low_band_side_ratio,stereo_high_band_side_ratio\n";
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
            << metrics.stereoSideRatio << ','
            << metrics.stereoLowBandSideRatio << ','
            << metrics.stereoHighBandSideRatio << '\n';

        std::cout << scenario.name
                  << " rms=" << metrics.sustainRms
                  << " tail=" << metrics.tailRms
                  << " peak=" << metrics.peak
                  << " periodicity=" << metrics.periodicity
                  << " centroid_hz=" << metrics.spectralCentroidHz
                  << " hf_ratio=" << metrics.highBandRatio
                  << " side_ratio=" << metrics.stereoSideRatio
                  << " low_side_ratio=" << metrics.stereoLowBandSideRatio
                  << " high_side_ratio=" << metrics.stereoHighBandSideRatio
                  << '\n';

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

    // Diagnostic only: locate where the held tone becomes cycle-locked.
    // No pass/fail threshold is attached to these values. The purpose is to
    // compare bow-contact force, string injection, bridge arrival, body motion
    // and radiated output on the same nominal period before/after physical
    // contact-model changes.
    {
        struct CycleProbe
        {
            const char* name;
            int stringIndex;
            int pairLower;
            float balance;
            float frequencyHz;
        };
        constexpr std::array<CycleProbe, 4> probes {{
            { "G3_open", 0, 0, -0.95f, 195.9977f },
            { "D4_open", 1, 1, -0.95f, 293.6648f },
            { "A4_open", 2, 1, +0.95f, 440.0000f },
            { "E5_open", 3, 2, +0.95f, 659.2551f },
        }};

        std::ofstream cycleCsv(
            outputDirectory / "cycle_similarity_diagnostics.csv");
        std::ofstream slipCsv(
            outputDirectory / "slip_timing_diagnostics.csv");
        if (!cycleCsv || !slipCsv)
            ok = false;
        else
        {
            slipCsv
                << "case,pitch_hz,slip_interval_count,"
                   "mean_interval_samples,target_period_samples,"
                   "mean_period_ratio,std_interval_samples,"
                   "std_fraction,min_interval_samples,"
                   "max_interval_samples\n"
                << std::setprecision(9);
            cycleCsv
                << "case,pitch_hz,point,separation_cycles,cycle_corr_mean,"
                   "cycle_corr_std,cycle_corr_min,cycle_corr_max,"
                   "cycle_pair_count,cycle_difference_ratio\n"
                << std::setprecision(9);
        }

        const auto begin =
            static_cast<std::size_t>(0.85 * sampleRate);
        const auto end =
            static_cast<std::size_t>(1.55 * sampleRate);

        for (const auto& probe : probes)
        {
            const auto trace = renderCycleTrace(
                probe.stringIndex,
                probe.pairLower,
                probe.balance,
                probe.frequencyHz);

            const auto slipStats = slipOnsetIntervalStats(
                trace.sticking, begin, end);
            const auto targetPeriod =
                sampleRate / static_cast<double>(probe.frequencyHz);
            const auto periodRatio = slipStats.intervalCount > 0
                ? slipStats.meanSamples / targetPeriod
                : 0.0;
            const auto stdFraction =
                slipStats.meanSamples > 0.0
                    ? slipStats.standardDeviationSamples
                        / slipStats.meanSamples
                    : 0.0;
            if (slipCsv)
            {
                slipCsv << probe.name << ',' << probe.frequencyHz
                    << ',' << slipStats.intervalCount
                    << ',' << slipStats.meanSamples
                    << ',' << targetPeriod
                    << ',' << periodRatio
                    << ',' << slipStats.standardDeviationSamples
                    << ',' << stdFraction
                    << ',' << slipStats.minimumSamples
                    << ',' << slipStats.maximumSamples << '\n';
            }

            const auto writePoint =
                [&](const char* point, const std::vector<float>& signal)
            {
                constexpr std::array<int, 4> separations {
                    1, 4, 16, 64
                };
                for (const auto separationCycles : separations)
                {
                    const auto stats = cycleCorrelationStats(
                        signal,
                        begin,
                        end,
                        probe.frequencyHz,
                        separationCycles);
                    const auto difference = cycleDifferenceRatio(
                        signal,
                        begin,
                        end,
                        probe.frequencyHz,
                        separationCycles);
                    if (cycleCsv)
                    {
                        cycleCsv << probe.name << ',' << probe.frequencyHz
                            << ',' << point
                            << ',' << separationCycles
                            << ',' << stats.mean
                            << ',' << stats.standardDeviation
                            << ',' << stats.minimum
                            << ',' << stats.maximum
                            << ',' << stats.pairCount
                            << ',' << difference << '\n';
                    }
                }
            };

            writePoint(
                "contact_friction_force",
                trace.contactFrictionForce);
            writePoint(
                "contact_grip_utilization",
                trace.contactGripUtilization);
            writePoint(
                "torsional_surface_velocity",
                trace.torsionalSurfaceVelocity);
            writePoint(
                "bow_injection_velocity",
                trace.bowInjectionVelocity);
            writePoint(
                "incident_bridge_velocity",
                trace.incidentBridgeVelocity);
            writePoint(
                "bridge_velocity",
                trace.bridgeVelocity);
            writePoint(
                "radiated_left",
                trace.radiated);
        }
    }

    // The new high E/A recordings are separate listening diagnostics.
    // The legacy acceptance suite's calibrated low-vs-high stereo checks
    // concern its D-string scenarios and remain entirely unchanged. For E5,
    // keep the actual measured stereo response visible rather than silently
    // treating those D-string ratios as instrument-wide ground truth.
    {
        std::array<Scenario, 3> highStringProbes{};
        auto a = baseControls();
        a.balance = -0.95f;
        highStringProbes[0] = { "13_A4_open_high_string", a, 440.0f, 0.85f };
        auto e = baseControls();
        e.balance = 0.95f;
        highStringProbes[1] = { "14_E5_open_high_string", e, 659.2551f, 0.85f };
        highStringProbes[2] = { "15_Fsharp5_stopped_E_string", e, 739.9888f, 0.85f };

        std::ofstream listeningCsv(
            outputDirectory / "high_string_listening_metrics.csv");
        if (!listeningCsv)
            ok = false;
        else
            listeningCsv << "scenario,periodicity,adjacent_cycle_difference_ratio,"
                            "centroid_hz,high_band_ratio,"
                            "harmonic_envelope_irregularity_db,"
                            "low_stereo_ratio,high_stereo_ratio,rms,peak\n";

        for (const auto& probe : highStringProbes)
        {
            const auto render = renderScenario(probe);
            const auto metrics = measure(render, probe.noteHz);
            // Universal audio safety checks, not a substitute for the
            // separately preserved tone/gesture/pitch acceptance suite.
            const bool safe = metrics.finite
                && metrics.sustainRms > 1.0e-5
                && metrics.peak * static_cast<double>(listeningGain) < 0.95
                && metrics.tailRms < metrics.sustainRms * 0.85 + 1.0e-8;
            if (!safe)
            {
                std::cerr << "FAIL: high-string listening safety "
                          << probe.name << '\n';
                ok = false;
            }
            if (listeningCsv)
                listeningCsv << probe.name << ',' << metrics.periodicity
                    << ',' << metrics.adjacentCycleDifferenceRatio
                    << ',' << metrics.spectralCentroidHz
                    << ',' << metrics.highBandRatio
                    << ',' << metrics.harmonicEnvelopeIrregularityDb
                    << ',' << metrics.stereoLowBandSideRatio
                    << ',' << metrics.stereoHighBandSideRatio
                    << ',' << metrics.sustainRms
                    << ',' << metrics.peak << '\n';
            if (!writeStereoWav16(
                outputDirectory / (probe.name + ".wav"),
                render.left, render.right))
            {
                std::cerr << "FAIL: cannot write high-string listening WAV "
                          << probe.name << '\n';
                ok = false;
            }
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

    // Player-control rosin showcase:
    // Slow Bow -> Fast Bow -> Light Pressure -> Firm Pressure.
    std::array<Scenario, 4> rosinPerformanceScenarios;
    {
        auto c = baseControls();
        c.speed = 0.25f;
        rosinPerformanceScenarios[0] = {
            "rosin_slow_bow", c, 329.6276f, 0.85f
        };
    }
    {
        auto c = baseControls();
        c.speed = 0.85f;
        rosinPerformanceScenarios[1] = {
            "rosin_fast_bow", c, 329.6276f, 0.85f
        };
    }
    {
        auto c = baseControls();
        c.pressure = 0.25f;
        rosinPerformanceScenarios[2] = {
            "rosin_light_pressure", c, 329.6276f, 0.85f
        };
    }
    {
        auto c = baseControls();
        c.pressure = 0.78f;
        rosinPerformanceScenarios[3] = {
            "rosin_firm_pressure", c, 329.6276f, 0.85f
        };
    }

    std::vector<float> rosinPerformanceLeft;
    std::vector<float> rosinPerformanceRight;
    for (const auto& scenario : rosinPerformanceScenarios)
    {
        const auto render = renderScenario(scenario);
        rosinPerformanceLeft.insert(
            rosinPerformanceLeft.end(), render.left.begin(), render.left.end());
        rosinPerformanceRight.insert(
            rosinPerformanceRight.end(), render.right.begin(), render.right.end());
        rosinPerformanceLeft.insert(
            rosinPerformanceLeft.end(), silenceSamples, 0.0f);
        rosinPerformanceRight.insert(
            rosinPerformanceRight.end(), silenceSamples, 0.0f);
    }

    if (!writeStereoWav16(
            outputDirectory / "13_rosin_performance_showcase.wav",
            rosinPerformanceLeft,
            rosinPerformanceRight))
    {
        std::cerr << "FAIL: cannot write rosin performance showcase WAV\n";
        ok = false;
    }

    // Same pitch, two physical strings. This is deliberately not a
    // sample-layer round robin: D-string A4 and open A4 should retain different
    // string impedance, stopped-string termination and bridge-rocking colour.
    const auto a4OnD = renderSamePitchOnString(
        1, 1, -0.95f, 440.0f, true);
    // Diagnostic alternate bow-pair: the same stopped D-string A4 approached
    // from the G/D side. The open A string remains physically present through
    // the shared bridge, but it is no longer the directly bowed neighbour.
    const auto a4OnDFromGSide =
        renderSamePitchOnString(1, 0, +0.95f, 440.0f, true);
    const auto openA = renderSamePitchOnString(
        2, 1, +0.95f, 440.0f, true);
    const auto identityBegin = static_cast<std::size_t>(0.85 * sampleRate);
    const auto identityEnd = static_cast<std::size_t>(1.55 * sampleRate);
    const auto stringIdentityDifference = differenceRms(
        a4OnD.left, openA.left, identityBegin, identityEnd);
    const auto a4OnDMetrics = measure(a4OnD, 440.0);
    const auto a4OnDGSideMetrics = measure(a4OnDFromGSide, 440.0);
    const auto openAMetrics = measure(openA, 440.0);
    const auto rockingWidthDifference = std::abs(
        a4OnDMetrics.stereoSideRatio - openAMetrics.stereoSideRatio);
    const auto pairSideCentroidRatio =
        std::min(
            a4OnDMetrics.spectralCentroidHz,
            a4OnDGSideMetrics.spectralCentroidHz)
        / std::max(
            a4OnDMetrics.spectralCentroidHz,
            a4OnDGSideMetrics.spectralCentroidHz);
    const auto pairSideWidthDifference = std::abs(
        a4OnDMetrics.stereoSideRatio
        - a4OnDGSideMetrics.stereoSideRatio);

    if (!std::isfinite(stringIdentityDifference)
        || stringIdentityDifference < 0.005
        || rockingWidthDifference < 0.003
        || pairSideCentroidRatio < 0.90
        || pairSideWidthDifference > 0.010)
    {
        std::cerr << "FAIL: same-pitch notes lost physical string identity"
                  << " difference_rms=" << stringIdentityDifference
                  << " D_side_ratio=" << a4OnDMetrics.stereoSideRatio
                  << " A_side_ratio=" << openAMetrics.stereoSideRatio
                  << " pair_side_centroid_ratio=" << pairSideCentroidRatio
                  << " pair_side_width_difference=" << pairSideWidthDifference
                  << '\n';
        ok = false;
    }

    std::vector<float> stringIdentityLeft;
    std::vector<float> stringIdentityRight;
    stringIdentityLeft.insert(
        stringIdentityLeft.end(), a4OnD.left.begin(), a4OnD.left.end());
    stringIdentityRight.insert(
        stringIdentityRight.end(), a4OnD.right.begin(), a4OnD.right.end());
    stringIdentityLeft.insert(
        stringIdentityLeft.end(), silenceSamples, 0.0f);
    stringIdentityRight.insert(
        stringIdentityRight.end(), silenceSamples, 0.0f);
    stringIdentityLeft.insert(
        stringIdentityLeft.end(), openA.left.begin(), openA.left.end());
    stringIdentityRight.insert(
        stringIdentityRight.end(), openA.right.begin(), openA.right.end());

    if (!writeStereoWav16(
            outputDirectory / "14_string_identity_A4_D_vs_A.wav",
            stringIdentityLeft,
            stringIdentityRight))
    {
        std::cerr << "FAIL: cannot write same-pitch string identity WAV\n";
        ok = false;
    }

    std::vector<float> pairSideLeft;
    std::vector<float> pairSideRight;
    const auto appendPairSide = [&](const Render& render)
    {
        pairSideLeft.insert(
            pairSideLeft.end(), render.left.begin(), render.left.end());
        pairSideRight.insert(
            pairSideRight.end(), render.right.begin(), render.right.end());
        pairSideLeft.insert(pairSideLeft.end(), silenceSamples, 0.0f);
        pairSideRight.insert(pairSideRight.end(), silenceSamples, 0.0f);
    };
    appendPairSide(a4OnD);
    appendPairSide(a4OnDFromGSide);
    appendPairSide(openA);

    if (!writeStereoWav16(
            outputDirectory / "15_string_identity_pair_side.wav",
            pairSideLeft,
            pairSideRight))
    {
        std::cerr << "FAIL: cannot write bow-pair string identity diagnostic WAV\n";
        ok = false;
    }

    std::ofstream stringIdentityCsv(
        outputDirectory / "string_identity_metrics.csv");
    if (!stringIdentityCsv)
    {
        std::cerr << "FAIL: cannot write string identity metrics CSV\n";
        ok = false;
    }
    else
    {
        stringIdentityCsv
            << "case,spectral_centroid_hz,high_band_ratio,stereo_side_ratio,"
               "stereo_low_band_side_ratio,stereo_high_band_side_ratio\n"
            << std::setprecision(9);
        const auto writeIdentityMetrics =
            [&](const char* name, const Metrics& m)
        {
            stringIdentityCsv
                << name << ','
                << m.spectralCentroidHz << ','
                << m.highBandRatio << ','
                << m.stereoSideRatio << ','
                << m.stereoLowBandSideRatio << ','
                << m.stereoHighBandSideRatio << '\n';
        };
        writeIdentityMetrics("D_A4_from_DA_side", a4OnDMetrics);
        writeIdentityMetrics("D_A4_from_GD_side", a4OnDGSideMetrics);
        writeIdentityMetrics("open_A4", openAMetrics);
    }

    {
        struct SameStringPitchProbe { const char* name; float hz; };
        constexpr std::array<SameStringPitchProbe, 4> probes {{
            { "D_string_E4", 329.6276f },
            { "D_string_Fsharp4", 369.9944f },
            { "D_string_G4", 391.9954f },
            { "D_string_A4", 440.0f },
        }};
        // Four sustained stopped notes on the SAME D string, at identical
        // bow controls. This isolates the F#4-to-A4 colour step heard in
        // the user's reel without changing the engine or pass/fail limits.
        std::vector<float> sweepLeft;
        std::vector<float> sweepRight;
        std::ofstream contactCsv(
            outputDirectory / "same_string_contact_dynamics.csv");
        if (contactCsv)
            contactCsv << "case,pitch_hz,sticking_fraction,release_events,"
                          "recatch_events,releases_per_cycle,contact_force_rms,"
                          "bow_injection_rms,bridge_incident_rms,radiated_rms,"
                          "mean_grip_utilization,static_fallback_fraction\n";
        else
            ok = false;
        std::ofstream sweepCsv(
            outputDirectory / "same_string_pitch_sweep.csv");
        if (!sweepCsv)
            ok = false;
        else
        {
            sweepCsv << "case,pitch_hz,centroid_hz,high_band_ratio,periodicity\n"
                     << std::setprecision(9);
            for (const auto& probe : probes)
            {
                const auto render = renderSamePitchOnString(
                    1, 1, -0.95f, probe.hz, true);
                const auto metrics = measure(render, probe.hz);
                sweepCsv << probe.name << ',' << probe.hz << ','
                         << metrics.spectralCentroidHz << ','
                         << metrics.highBandRatio << ','
                         << metrics.periodicity << '\n';
                // Same bow and same speaking length as the listening WAV.
                // Count real friction state transitions to distinguish a
                // bow-contact bifurcation from fixed body-formant filtering.
                const auto trace = renderCycleTrace(
                    1, 1, -0.95f, probe.hz);
                const auto first = std::min(
                    trace.sticking.size(),
                    static_cast<std::size_t>(0.30 * sampleRate));
                const auto last = trace.sticking.size();
                double stickingCount = 0.0;
                double frictionEnergy = 0.0;
                double injectionEnergy = 0.0;
                double bridgeEnergy = 0.0;
                double audioEnergy = 0.0;
                double gripSum = 0.0;
                double fallbackCount = 0.0;
                std::size_t releases = 0, recatches = 0;
                for (std::size_t k = first; k < last; ++k)
                {
                    stickingCount += trace.sticking[k] != 0 ? 1.0 : 0.0;
                    if (k > first && trace.sticking[k] != trace.sticking[k-1])
                    {
                        if (trace.sticking[k] == 0) ++releases;
                        else ++recatches;
                    }
                    const auto force = static_cast<double>(trace.contactFrictionForce[k]);
                    const auto inj = static_cast<double>(trace.bowInjectionVelocity[k]);
                    const auto bridge = static_cast<double>(trace.incidentBridgeVelocity[k]);
                    const auto audio = static_cast<double>(trace.radiated[k]);
                    frictionEnergy += force * force;
                    injectionEnergy += inj * inj;
                    bridgeEnergy += bridge * bridge;
                    audioEnergy += audio * audio;
                    gripSum += trace.contactGripUtilization[k];
                    fallbackCount += trace.staticFallback[k] != 0 ? 1.0 : 0.0;
                }
                const auto span = std::max<std::size_t>(1, last-first);
                if (contactCsv)
                    contactCsv << probe.name << ',' << probe.hz << ','
                               << stickingCount/span << ',' << releases << ','
                               << recatches << ','
                               << releases / ((last-first)*probe.hz/sampleRate)
                               << ',' << std::sqrt(frictionEnergy/span)
                               << ',' << std::sqrt(injectionEnergy/span)
                               << ',' << std::sqrt(bridgeEnergy/span)
                               << ',' << std::sqrt(audioEnergy/span)
                               << ',' << gripSum/span
                               << ',' << fallbackCount/span << '\n';
                sweepLeft.insert(
                    sweepLeft.end(), render.left.begin(), render.left.end());
                sweepRight.insert(
                    sweepRight.end(), render.right.begin(), render.right.end());
                sweepLeft.insert(sweepLeft.end(), silenceSamples, 0.0f);
                sweepRight.insert(sweepRight.end(), silenceSamples, 0.0f);
            }
        }
        if (!writeStereoWav16(
                outputDirectory / "18_D_string_pitch_sweep.wav",
                sweepLeft, sweepRight))
        {
            std::cerr << "FAIL: cannot write D-string pitch sweep WAV\n";
            ok = false;
        }
    }


    // A-string upper-register bowed tones occur repeatedly in the reel's
    // D5 -> E5 passage. Identify whether the plucked-sounding upper note is
    // a bowed-string contact bifurcation, not just an open-E crossing.
    {
        struct UpperNote { const char* name; float hz; };
        constexpr std::array<UpperNote, 5> notes {{
            {"A_string_B4", 493.8833f},
            {"A_string_Csharp5", 554.3653f},
            {"A_string_D5", 587.3295f},
            {"A_string_Dsharp5", 622.2540f},
            {"A_string_E5", 659.2551f}
        }};
        std::ofstream upper(
            outputDirectory / "A_string_high_fingering_contact.csv");
        if (!upper)
            ok = false;
        else
        {
            upper << "note,hz,sticking_fraction,release_count,"
                     "releases_per_cycle,normal_force_rms,"
                     "bridge_incident_rms,output_rms,centroid_hz,"
                     "high_band_ratio\n";
            for (const auto& note : notes)
            {
                const auto render = renderSamePitchOnString(
                    2, 2, -0.95f, note.hz, true);
                const auto metrics = measure(render, note.hz);
                const auto trace = renderCycleTrace(
                    2, 2, -0.95f, note.hz);
                const auto first = std::min(
                    trace.sticking.size(),
                    static_cast<std::size_t>(0.30 * sampleRate));
                const auto span = trace.sticking.size() - first;
                double stickingCount = 0.0, forceEnergy = 0.0,
                       bridgeEnergy = 0.0, audioEnergy = 0.0;
                std::size_t releases = 0;
                for (std::size_t k = first; k < trace.sticking.size(); ++k)
                {
                    stickingCount += trace.sticking[k] ? 1.0 : 0.0;
                    if (k > first && trace.sticking[k-1]
                                  && !trace.sticking[k])
                        ++releases;
                    const auto force = static_cast<double>(
                        trace.contactFrictionForce[k]);
                    const auto bridge = static_cast<double>(
                        trace.incidentBridgeVelocity[k]);
                    const auto radiated = static_cast<double>(
                        trace.radiated[k]);
                    forceEnergy += force*force;
                    bridgeEnergy += bridge*bridge;
                    audioEnergy += radiated*radiated;
                }
                const auto denominator =
                    static_cast<double>(std::max<std::size_t>(1,span));
                upper << note.name << ',' << note.hz << ','
                      << stickingCount/denominator << ',' << releases
                      << ',' << releases/(denominator*note.hz/sampleRate)
                      << ',' << std::sqrt(forceEnergy/denominator)
                      << ',' << std::sqrt(bridgeEnergy/denominator)
                      << ',' << std::sqrt(audioEnergy/denominator)
                      << ',' << metrics.spectralCentroidHz
                      << ',' << metrics.highBandRatio << '\n';
            }
        }
    }


    // Musical E5 is generally fingered on the A string by the Play-mode
    // left hand. At this exact pitch, the *unused open E string* is tuned
    // to unison and receives bridge-transmitted energy. A too-strong
    // sympathetically resonating open E can make a bowed A-string note
    // sound as if a separate bell/pluck is ringing. Measure the physical
    // travelling-wave energy on both strings before changing any losses.
    {
        std::ofstream sympatheticCsv(
            outputDirectory / "A_E_unison_sympathetic_metrics.csv");
        if (!sympatheticCsv)
            ok = false;
        else
        {
            sympatheticCsv << "case,primary_hz,bowed_A_incident_rms,"
                              "open_E_incident_rms,E_over_A_rms,"
                              "radiated_rms,E_tail_rms\n";
            constexpr std::array<float, 5> notes {
                554.3653f, 587.3295f, 622.2540f, 659.2551f, 698.4565f
            };
            constexpr std::array<const char*, 5> labels {
                "Csharp5_A", "D5_A", "Dsharp5_A", "E5_A_unison", "F5_A"
            };
            for (std::size_t n = 0; n < notes.size(); ++n)
            {
                fiddle::FiddleEngine sim;
                sim.prepare(sampleRate);
                auto controls = baseControls();
                controls.balance = -0.95f;
                controls.singleStringIsolation = 1.0f;
                sim.setControls(controls);
                std::array<float, 4> fingering {};
                fingering[2] = notes[n];
                sim.setFingeringLayout(fingering, 2, 2, 0.85f);
                sim.startBow(+1);

                double aPower = 0.0, ePower = 0.0, soundPower = 0.0;
                constexpr double duration = 1.1;
                const auto total = static_cast<std::size_t>(
                    duration * sampleRate);
                const auto start = static_cast<std::size_t>(
                    0.35 * sampleRate);
                for (std::size_t i = 0; i < total; ++i)
                {
                    float l = 0.0f, r = 0.0f;
                    sim.process(&l, &r, 1);
                    if (i < start) continue;
                    const auto debug = sim.debugSnapshot();
                    const auto a = static_cast<double>(
                        debug.incidentBridgeVelocityMps[2]);
                    const auto e = static_cast<double>(
                        debug.incidentBridgeVelocityMps[3]);
                    aPower += a * a;
                    ePower += e * e;
                    soundPower += static_cast<double>(l) * l;
                }
                const auto count = std::max<std::size_t>(1, total-start);
                const auto aRms = std::sqrt(aPower/count);
                const auto eRms = std::sqrt(ePower/count);
                const auto outRms = std::sqrt(soundPower/count);

                sim.stopBow();
                double eTailPower = 0.0;
                const auto tailSamples = static_cast<std::size_t>(
                    0.085 * sampleRate);
                for (std::size_t i = 0; i < tailSamples; ++i)
                {
                    float l = 0.0f, r = 0.0f;
                    sim.process(&l, &r, 1);
                    const auto debug = sim.debugSnapshot();
                    const auto e = static_cast<double>(
                        debug.incidentBridgeVelocityMps[3]);
                    eTailPower += e * e;
                }
                const auto eTailRms = std::sqrt(eTailPower /
                    std::max<std::size_t>(1,tailSamples));
                sympatheticCsv << labels[n] << ',' << notes[n] << ','
                               << aRms << ',' << eRms << ','
                               << eRms/(aRms+1.0e-12) << ','
                               << outRms << ',' << eTailRms << '\n';
            }
        }
    }

    std::cout << "string_identity_A4_D_vs_A_difference_rms="
              << stringIdentityDifference << '\n'
              << "string_identity_A4_on_D_side_ratio="
              << a4OnDMetrics.stereoSideRatio << '\n'
              << "string_identity_A4_on_D_DA_centroid="
              << a4OnDMetrics.spectralCentroidHz << '\n'
              << "string_identity_A4_on_D_GD_centroid="
              << a4OnDGSideMetrics.spectralCentroidHz << '\n'
              << "string_identity_open_A_centroid="
              << openAMetrics.spectralCentroidHz << '\n'
              << "string_identity_open_A_side_ratio="
              << openAMetrics.stereoSideRatio << '\n'
              << "string_identity_pair_side_centroid_ratio="
              << pairSideCentroidRatio << '\n'
              << "string_identity_pair_side_width_difference="
              << pairSideWidthDifference << '\n';

    // Adjacent-string identity matrix: each row compares the same pitch
    // played as a stopped note on the lower string and as the next open string.
    // This does not require them to sound identical; it records whether the
    // physical string family changes smoothly across G/D, D/A and A/E.
    struct IdentityPair
    {
        const char* name;
        int lowerString;
        int upperString;
        float pitchHz;
    };
    constexpr std::array<IdentityPair, 3> identityPairs {{
        { "G_to_open_D", 0, 1, 293.6648f },
        { "D_to_open_A", 1, 2, 440.0f },
        { "A_to_open_E", 2, 3, 659.2551f }
    }};

    std::ofstream identityMatrixCsv(
        outputDirectory / "string_identity_matrix.csv");
    if (!identityMatrixCsv)
    {
        std::cerr << "FAIL: cannot write string identity matrix CSV\n";
        ok = false;
    }
    else
    {
        identityMatrixCsv
            << "pair,lower_string_centroid_hz,open_upper_centroid_hz,"
               "centroid_ratio,lower_high_band_ratio,open_upper_high_band_ratio,"
               "lower_side_ratio,open_upper_side_ratio,difference_rms\n"
            << std::setprecision(9);

        std::vector<float> matrixLeft;
        std::vector<float> matrixRight;
        for (const auto& pair : identityPairs)
        {
            const auto pairLower = std::min(pair.lowerString, 2);
            const auto lowerBalance =
                pair.lowerString == pairLower ? -0.95f : +0.95f;
            const auto upperPairLower = std::max(0, pair.upperString - 1);
            const auto upperBalance =
                pair.upperString == upperPairLower ? -0.95f : +0.95f;

            const auto stoppedLower = renderSamePitchOnString(
                pair.lowerString,
                pairLower,
                lowerBalance,
                pair.pitchHz,
                true);
            const auto openUpper = renderSamePitchOnString(
                pair.upperString,
                upperPairLower,
                upperBalance,
                pair.pitchHz,
                true);

            const auto lowerMetrics = measure(stoppedLower, pair.pitchHz);
            const auto upperMetrics = measure(openUpper, pair.pitchHz);
            const auto centroidRatio =
                lowerMetrics.spectralCentroidHz
                / std::max(1.0, upperMetrics.spectralCentroidHz);
            const auto diff = differenceRms(
                stoppedLower.left,
                openUpper.left,
                identityBegin,
                identityEnd);

            identityMatrixCsv
                << pair.name << ','
                << lowerMetrics.spectralCentroidHz << ','
                << upperMetrics.spectralCentroidHz << ','
                << centroidRatio << ','
                << lowerMetrics.highBandRatio << ','
                << upperMetrics.highBandRatio << ','
                << lowerMetrics.stereoSideRatio << ','
                << upperMetrics.stereoSideRatio << ','
                << diff << '\n';

            if (!std::isfinite(centroidRatio)
                || centroidRatio < 0.45
                || centroidRatio > 3.0
                || diff < 0.003)
            {
                std::cerr
                    << "FAIL: adjacent string identity matrix out of bounds"
                    << " pair=" << pair.name
                    << " centroid_ratio=" << centroidRatio
                    << " difference_rms=" << diff << '\n';
                ok = false;
            }

            matrixLeft.insert(
                matrixLeft.end(),
                stoppedLower.left.begin(),
                stoppedLower.left.end());
            matrixRight.insert(
                matrixRight.end(),
                stoppedLower.right.begin(),
                stoppedLower.right.end());
            matrixLeft.insert(matrixLeft.end(), silenceSamples, 0.0f);
            matrixRight.insert(matrixRight.end(), silenceSamples, 0.0f);
            matrixLeft.insert(
                matrixLeft.end(),
                openUpper.left.begin(),
                openUpper.left.end());
            matrixRight.insert(
                matrixRight.end(),
                openUpper.right.begin(),
                openUpper.right.end());
            matrixLeft.insert(matrixLeft.end(), silenceSamples, 0.0f);
            matrixRight.insert(matrixRight.end(), silenceSamples, 0.0f);

            std::cout
                << "string_identity_matrix_" << pair.name
                << "_centroid_ratio=" << centroidRatio
                << " lower_centroid=" << lowerMetrics.spectralCentroidHz
                << " upper_centroid=" << upperMetrics.spectralCentroidHz
                << " difference_rms=" << diff << '\n';
        }

        if (!writeStereoWav16(
                outputDirectory / "17_string_identity_matrix.wav",
                matrixLeft,
                matrixRight))
        {
            std::cerr << "FAIL: cannot write string identity matrix WAV\n";
            ok = false;
        }
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
