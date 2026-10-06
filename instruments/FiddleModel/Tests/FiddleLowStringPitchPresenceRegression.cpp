#include "Dsp/FiddleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;

struct ProbeMetrics
{
    double fundamentalFraction = 0.0;
    double low3Fraction = 0.0;
    double high4to8Fraction = 0.0;
    double harmonicCombPower = 0.0;
    double fixedBodyFormantPower = 0.0;
    double movingVsFixedDb = 0.0;
    double periodicity = 0.0;
    double harmonicLineFraction = 0.0;
    int strongestHarmonic = 1;
};

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

double goertzelPower(const std::vector<float>& x,
                     std::size_t begin,
                     std::size_t end,
                     double frequency)
{
    begin = std::min(begin, x.size());
    end = std::min(end, x.size());
    if (frequency <= 0.0 || end <= begin + 64)
        return 0.0;

    const auto length = end - begin;
    const auto omega = 2.0 * pi * frequency / sampleRate;
    const auto coeff = 2.0 * std::cos(omega);
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;

    for (std::size_t i = 0; i < length; ++i)
    {
        const auto window =
            0.5 - 0.5 * std::cos(
                2.0 * pi * static_cast<double>(i)
                / static_cast<double>(length - 1));
        s0 = static_cast<double>(x[begin + i]) * window
            + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return std::max(
        0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

ProbeMetrics measure(const std::vector<float>& x,
                     double fundamental)
{
    const auto end = x.size();
    const auto length = std::min<std::size_t>(
        end, static_cast<std::size_t>(0.24 * sampleRate));
    const auto begin = end - length;

    std::array<double, 8> power {};
    double total = 0.0;
    double strongest = -1.0;
    int strongestHarmonic = 1;

    for (int harmonic = 1; harmonic <= 8; ++harmonic)
    {
        power[static_cast<std::size_t>(harmonic - 1)] =
            goertzelPower(
                x, begin, end, fundamental * harmonic);
        total += power[static_cast<std::size_t>(harmonic - 1)];

        if (power[static_cast<std::size_t>(harmonic - 1)]
            > strongest)
        {
            strongest =
                power[static_cast<std::size_t>(harmonic - 1)];
            strongestHarmonic = harmonic;
        }
    }

    ProbeMetrics metrics;
    if (total > 1.0e-30)
    {
        metrics.fundamentalFraction = power[0] / total;
        metrics.low3Fraction =
            (power[0] + power[1] + power[2]) / total;
        metrics.high4to8Fraction =
            1.0 - metrics.low3Fraction;
    }
    metrics.harmonicCombPower = total;

    // Track the stationary body/bridge-formant region separately from the
    // note-locked harmonic comb. These are the unchanged upper translation
    // modes that can perceptually read as a fixed "front" pitch/timbre while
    // the stopped-string pitch survives only behind them.
    constexpr std::array<double, 6> fixedBodyFormants {
        1180.0, 1500.0, 1900.0, 2350.0, 2850.0, 3500.0
    };
    for (const auto frequency : fixedBodyFormants)
        metrics.fixedBodyFormantPower +=
            goertzelPower(x, begin, end, frequency);

    metrics.movingVsFixedDb =
        10.0 * std::log10(
            (metrics.harmonicCombPower + 1.0e-30)
            / (metrics.fixedBodyFormantPower + 1.0e-30));

    metrics.periodicity =
        periodicityAtFrequency(x, begin, end, fundamental);

    // Unlike the earlier harmonic fractions, this denominator includes the
    // entire audible low/mid spectrum. It therefore exposes the player-
    // reported failure mode where a weak pitched comb sits behind a much
    // louder bow/string-like broadband component.
    constexpr std::size_t spectralLength = 4096;
    const auto spectralBegin =
        end > spectralLength ? end - spectralLength : begin;
    const auto spectralEnd =
        std::min(end, spectralBegin + spectralLength);
    const auto actualLength = spectralEnd - spectralBegin;
    double totalSpectralPower = 0.0;
    double harmonicLinePower = 0.0;
    if (actualLength >= 512)
    {
        const auto binHz =
            sampleRate / static_cast<double>(actualLength);
        const auto firstBin = static_cast<int>(std::ceil(80.0 / binHz));
        const auto lastBin = static_cast<int>(std::floor(8000.0 / binHz));

        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            const auto frequency = static_cast<double>(bin) * binHz;
            const auto powerAtBin =
                goertzelPower(x, spectralBegin, spectralEnd, frequency);
            totalSpectralPower += powerAtBin;

            const auto harmonic =
                static_cast<int>(std::llround(frequency / fundamental));
            if (harmonic >= 1 && harmonic <= 16
                && std::abs(
                    frequency - fundamental * static_cast<double>(harmonic))
                    <= 1.5 * binHz)
            {
                harmonicLinePower += powerAtBin;
            }
        }
    }
    metrics.harmonicLineFraction =
        harmonicLinePower / (totalSpectralPower + 1.0e-30);
    metrics.strongestHarmonic = strongestHarmonic;
    return metrics;
}

void renderProbe(int stringIndex,
                 int pairLower,
                 float balance,
                 float targetHz,
                 float pressure,
                 float speed,
                 float position,
                 bool bowCatch,
                 std::vector<float>& incident,
                 std::vector<float>& injection,
                 std::vector<float>& radiated)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = pressure;
    controls.speed = speed;
    controls.attack = 0.55f;
    controls.position = position;
    controls.balance = balance;
    controls.singleStringIsolation = 1.0f;
    controls.vibratoWidth = 0.0f;
    controls.vibratoPace = 0.5f;
    engine.setControls(controls);

    std::array<float, 4> layout {};
    layout[static_cast<std::size_t>(stringIndex)] = targetHz;
    engine.setFingeringLayout(
        layout, stringIndex, pairLower, 0.85f);
    if (bowCatch)
        engine.setStrokeBite(0.064f, 0.007f);
    engine.startBow(+1);

    const auto totalSamples =
        static_cast<std::size_t>(0.70 * sampleRate);
    incident.reserve(totalSamples);
    injection.reserve(totalSamples);
    radiated.reserve(totalSamples);

    for (std::size_t sample = 0; sample < totalSamples; ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);
        const auto debug = engine.debugSnapshot();

        incident.push_back(
            debug.incidentBridgeVelocityMps[
                static_cast<std::size_t>(stringIndex)]);
        injection.push_back(
            debug.bowInjectionVelocityMps[
                static_cast<std::size_t>(stringIndex)]);
        radiated.push_back(left);
    }
}

void printMetrics(const char* name,
                  const char* point,
                  const ProbeMetrics& m)
{
    std::cout
        << "low_string_probe"
        << " case=" << name
        << " point=" << point
        << " fundamental_fraction=" << m.fundamentalFraction
        << " low3_fraction=" << m.low3Fraction
        << " high4to8_fraction=" << m.high4to8Fraction
        << " harmonic_comb_power=" << m.harmonicCombPower
        << " fixed_body_formant_power=" << m.fixedBodyFormantPower
        << " moving_vs_fixed_db=" << m.movingVsFixedDb
        << " periodicity=" << m.periodicity
        << " harmonic_line_fraction=" << m.harmonicLineFraction
        << " strongest_harmonic=" << m.strongestHarmonic
        << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    struct Case
    {
        const char* name;
        int stringIndex;
        int pairLower;
        float balance;
        float targetHz;
        bool diagnosticOnly;
        float pressure = 0.55f;
        float speed = 0.60f;
        float position = 0.45f;
        bool bowCatch = false;
    };

    // Compare the validated reference controls with individual Play defaults
    // and their combined physical bow gesture. This diagnoses why identical
    // G/D speaking lengths can sound pitched in the direct engine probe but
    // bright and detached in the actual Processor MIDI audition. All original
    // stopped-note acceptance conditions remain unchanged.
    constexpr std::array<Case, 12> cases {{
        { "G_open3", 0, 0, -0.95f, 195.9977f, true },
        { "G_Gsharp3", 0, 0, -0.95f, 207.65235f, false },
        { "D_open4", 1, 1, -0.95f, 293.6648f, true },
        { "D_E4", 1, 1, -0.95f, 329.62756f, false },
        { "A_Bflat4", 2, 2, -0.95f, 466.16376f, false },
        { "E_F5", 3, 2, +0.95f, 698.45646f, false },
        { "G_open3_position050", 0, 0, -0.95f, 195.9977f,
          true, 0.55f, 0.60f, 0.50f, false },
        { "D_open4_position050", 1, 1, -0.95f, 293.6648f,
          true, 0.55f, 0.60f, 0.50f, false },
        { "G_open3_pressure0504", 0, 0, -0.95f, 195.9977f,
          true, 0.504f, 0.60f, 0.45f, false },
        { "D_open4_pressure0529", 1, 1, -0.95f, 293.6648f,
          true, 0.529f, 0.60f, 0.45f, false },
        { "G_open3_play_bow", 0, 0, -0.95f, 195.9977f,
          true, 0.504f, 0.598f, 0.50f, true },
        { "D_open4_play_bow", 1, 1, -0.95f, 293.6648f,
          true, 0.529f, 0.598f, 0.50f, true }
    }};

    std::ofstream csv;
    if (argc >= 2)
    {
        const std::filesystem::path outputDirectory(argv[1]);
        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            std::cerr << "FAIL: cannot create probe output directory\n";
            return EXIT_FAILURE;
        }
        csv.open(outputDirectory / "low_string_pitch_presence.csv");
        if (!csv)
        {
            std::cerr << "FAIL: cannot create low-string probe CSV\n";
            return EXIT_FAILURE;
        }
        csv
            << "case,point,fundamental_fraction,low3_fraction,"
               "high4to8_fraction,harmonic_comb_power,"
               "fixed_body_formant_power,moving_vs_fixed_db,"
               "periodicity,harmonic_line_fraction,strongest_harmonic\n"
            << std::setprecision(9);
    }

    for (const auto& item : cases)
    {
        std::vector<float> incident;
        std::vector<float> injection;
        std::vector<float> radiated;
        renderProbe(
            item.stringIndex,
            item.pairLower,
            item.balance,
            item.targetHz,
            item.pressure,
            item.speed,
            item.position,
            item.bowCatch,
            incident,
            injection,
            radiated);

        const auto incidentMetrics =
            measure(incident, item.targetHz);
        const auto injectionMetrics =
            measure(injection, item.targetHz);
        const auto radiatedMetrics =
            measure(radiated, item.targetHz);

        printMetrics(item.name, "incident_bridge", incidentMetrics);
        printMetrics(item.name, "bow_injection", injectionMetrics);
        printMetrics(item.name, "radiated", radiatedMetrics);

        if (csv)
        {
            const auto write =
                [&](const char* point, const ProbeMetrics& m)
            {
                csv << item.name << ','
                    << point << ','
                    << m.fundamentalFraction << ','
                    << m.low3Fraction << ','
                    << m.high4to8Fraction << ','
                    << m.harmonicCombPower << ','
                    << m.fixedBodyFormantPower << ','
                    << m.movingVsFixedDb << ','
                    << m.periodicity << ','
                    << m.harmonicLineFraction << ','
                    << m.strongestHarmonic << '\n';
            };
            write("incident_bridge", incidentMetrics);
            write("bow_injection", injectionMetrics);
            write("radiated", radiatedMetrics);
        }

        if (!(std::isfinite(radiatedMetrics.low3Fraction)
              && radiatedMetrics.low3Fraction > 0.0
              && std::isfinite(radiatedMetrics.movingVsFixedDb)
              && std::isfinite(radiatedMetrics.periodicity)
              && std::isfinite(radiatedMetrics.harmonicLineFraction)))
        {
            std::cerr << "FAIL: low-string probe produced invalid spectrum\n";
            return EXIT_FAILURE;
        }

        // Player-reported failure mode: the correct pitch existed only as a
        // tiny synth-like component behind a much louder body/formant sound.
        // Require the first three harmonics to carry perceptually meaningful
        // energy on the two low strings, not merely be detectable by a
        // narrow-band pitch estimator.
        if (!item.diagnosticOnly
            && ((item.stringIndex == 0
             && radiatedMetrics.low3Fraction < 0.36)
            || (item.stringIndex == 1
                && radiatedMetrics.low3Fraction < 0.56)
            || (item.stringIndex == 2
                && radiatedMetrics.low3Fraction < 0.32)))
        {
            std::cerr
                << "FAIL: low-string pitch harmonics are masked by body/formant energy"
                << " string=" << item.stringIndex
                << " low3_fraction=" << radiatedMetrics.low3Fraction
                << '\n';
            return EXIT_FAILURE;
        }
    }

    std::cout << "PASS low-string physical-path probe\n";
    return EXIT_SUCCESS;
}
