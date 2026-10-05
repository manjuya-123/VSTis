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
    int strongestHarmonic = 1;
};

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
    metrics.strongestHarmonic = strongestHarmonic;
    return metrics;
}

void renderProbe(int stringIndex,
                 int pairLower,
                 float balance,
                 float targetHz,
                 std::vector<float>& incident,
                 std::vector<float>& injection,
                 std::vector<float>& radiated)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.55f;
    controls.speed = 0.60f;
    controls.attack = 0.55f;
    controls.position = 0.45f;
    controls.balance = balance;
    controls.singleStringIsolation = 1.0f;
    controls.vibratoWidth = 0.0f;
    controls.vibratoPace = 0.5f;
    engine.setControls(controls);

    std::array<float, 4> layout {};
    layout[static_cast<std::size_t>(stringIndex)] = targetHz;
    engine.setFingeringLayout(
        layout, stringIndex, pairLower, 0.85f);
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
    };

    constexpr std::array<Case, 4> cases {{
        { "G_Gsharp3", 0, 0, -0.95f, 207.65235f },
        { "D_E4", 1, 1, -0.95f, 329.62756f },
        { "A_Bflat4", 2, 2, -0.95f, 466.16376f },
        { "E_F5", 3, 2, +0.95f, 698.45646f }
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
               "high4to8_fraction,strongest_harmonic\n"
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
                    << m.strongestHarmonic << '\n';
            };
            write("incident_bridge", incidentMetrics);
            write("bow_injection", injectionMetrics);
            write("radiated", radiatedMetrics);
        }

        if (!(std::isfinite(radiatedMetrics.low3Fraction)
              && radiatedMetrics.low3Fraction > 0.0))
        {
            std::cerr << "FAIL: low-string probe produced invalid spectrum\n";
            return EXIT_FAILURE;
        }

        // Player-reported failure mode: the correct pitch existed only as a
        // tiny synth-like component behind a much louder body/formant sound.
        // Require the first three harmonics to carry perceptually meaningful
        // energy on the two low strings, not merely be detectable by a
        // narrow-band pitch estimator.
        if ((item.stringIndex == 0
             && radiatedMetrics.low3Fraction < 0.36)
            || (item.stringIndex == 1
                && radiatedMetrics.low3Fraction < 0.56)
            || (item.stringIndex == 2
                && radiatedMetrics.low3Fraction < 0.32))
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
