#include "Dsp/FiddleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
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
constexpr float listeningGain = 7.9432823f; // +18 dB plugin default
constexpr double renderSeconds = 0.72;
constexpr double measureStartSeconds = 0.42;
constexpr std::size_t spectralLength = 1024;

struct Metrics
{
    double rms = 0.0;
    double peak = 0.0;
    double centroidHz = 0.0;
    double highBandRatio = 0.0;
    double stickingFraction = 0.0;
    double meanGripUtilization = 0.0;
    double maxContactTemperatureC = 0.0;
    bool finite = true;
};

double goertzelPower(const std::vector<double>& x, double frequency)
{
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

Metrics renderPoint(float pressure, float speed, float position)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = pressure;
    controls.speed = speed;
    controls.attack = 0.62f;
    controls.position = position;
    controls.balance = -0.95f;
    controls.singleStringIsolation = 1.0f;
    controls.vibratoWidth = 0.0f;
    controls.vibratoPace = 0.5f;
    engine.setControls(controls);

    std::array<float, 4> fingering {};
    fingering[1] = 329.6276f; // E4 on D
    engine.setFingeringLayout(fingering, 1, 1, 0.88f);
    engine.startBow(+1);

    const auto totalSamples =
        static_cast<std::size_t>(renderSeconds * sampleRate);
    const auto measureBegin =
        static_cast<std::size_t>(measureStartSeconds * sampleRate);

    std::vector<float> measured;
    measured.reserve(totalSamples - measureBegin);

    double energy = 0.0;
    double peak = 0.0;
    double gripSum = 0.0;
    double maxTemperature = 0.0;
    std::size_t stickingCount = 0;
    std::size_t debugCount = 0;
    bool finite = true;

    for (std::size_t i = 0; i < totalSamples; ++i)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);

        finite = finite
            && std::isfinite(left)
            && std::isfinite(right);

        if (i >= measureBegin)
        {
            measured.push_back(left);
            energy += static_cast<double>(left) * left;
            peak = std::max(peak, std::abs(static_cast<double>(left)));

            if ((i & 31u) == 0u)
            {
                const auto debug = engine.debugSnapshot();
                const auto stringIndex = std::size_t { 1 };
                stickingCount += debug.sticking[stringIndex] ? 1u : 0u;
                gripSum += debug.contactGripUtilization[stringIndex];
                maxTemperature = std::max(
                    maxTemperature,
                    static_cast<double>(
                        debug.contactTemperatureC[stringIndex]));
                ++debugCount;
            }
        }
    }

    Metrics metrics;
    metrics.finite = finite;
    metrics.rms = measured.empty()
        ? 0.0
        : std::sqrt(energy / static_cast<double>(measured.size()));
    metrics.peak = peak;
    metrics.stickingFraction = debugCount > 0
        ? static_cast<double>(stickingCount)
            / static_cast<double>(debugCount)
        : 0.0;
    metrics.meanGripUtilization = debugCount > 0
        ? gripSum / static_cast<double>(debugCount)
        : 0.0;
    metrics.maxContactTemperatureC = maxTemperature;

    if (measured.size() >= spectralLength)
    {
        const auto begin = measured.size() - spectralLength;
        std::vector<double> segment(spectralLength);
        double mean = 0.0;
        for (std::size_t i = 0; i < spectralLength; ++i)
            mean += measured[begin + i];
        mean /= static_cast<double>(spectralLength);

        for (std::size_t i = 0; i < spectralLength; ++i)
        {
            const auto window = 0.5 - 0.5 * std::cos(
                2.0 * 3.14159265358979323846
                * static_cast<double>(i)
                / static_cast<double>(spectralLength - 1));
            segment[i] =
                (static_cast<double>(measured[begin + i]) - mean)
                * window;
        }

        double totalPower = 0.0;
        double highPower = 0.0;
        double weighted = 0.0;
        const auto firstBin = static_cast<int>(std::ceil(
            100.0 * spectralLength / sampleRate));
        const auto lastBin = static_cast<int>(std::floor(
            8000.0 * spectralLength / sampleRate));

        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            const auto frequency =
                static_cast<double>(bin) * sampleRate
                / static_cast<double>(spectralLength);
            const auto power = goertzelPower(segment, frequency);
            totalPower += power;
            weighted += frequency * power;
            if (frequency >= 2500.0)
                highPower += power;
        }

        if (totalPower > 0.0)
        {
            metrics.centroidHz = weighted / totalPower;
            metrics.highBandRatio = highPower / totalPower;
        }
    }

    return metrics;
}

double dbRatio(double a, double b)
{
    return 20.0 * std::log10(
        std::max(a, 1.0e-12) / std::max(b, 1.0e-12));
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
        std::cerr << "FAIL: cannot create output directory\n";
        return EXIT_FAILURE;
    }

    constexpr std::array<float, 5> pressures {
        0.18f, 0.34f, 0.50f, 0.66f, 0.82f
    };
    constexpr std::array<float, 5> speeds {
        0.18f, 0.34f, 0.50f, 0.66f, 0.82f
    };
    constexpr std::array<float, 3> positions {
        0.10f, 0.50f, 0.90f
    };

    std::ofstream csv(outputDirectory / "control_surface_metrics.csv");
    if (!csv)
    {
        std::cerr << "FAIL: cannot create control_surface_metrics.csv\n";
        return EXIT_FAILURE;
    }
    csv << "position,pressure,speed,rms,peak,audition_peak,"
           "centroid_hz,high_band_ratio,sticking_fraction,"
           "mean_grip_utilization,max_contact_temperature_c\n";
    csv << std::setprecision(9);

    using Grid =
        std::array<std::array<Metrics, speeds.size()>, pressures.size()>;
    std::array<Grid, positions.size()> grids {};

    bool ok = true;
    double maxAdjacentRmsJumpDb = 0.0;
    double maxAdjacentCentroidRatio = 1.0;
    double maxAuditionPeak = 0.0;

    for (std::size_t z = 0; z < positions.size(); ++z)
    {
        for (std::size_t p = 0; p < pressures.size(); ++p)
        {
            for (std::size_t s = 0; s < speeds.size(); ++s)
            {
                const auto metrics =
                    renderPoint(pressures[p], speeds[s], positions[z]);
                grids[z][p][s] = metrics;
                const auto auditionPeak =
                    metrics.peak * static_cast<double>(listeningGain);
                maxAuditionPeak = std::max(maxAuditionPeak, auditionPeak);

                csv << positions[z] << ','
                    << pressures[p] << ','
                    << speeds[s] << ','
                    << metrics.rms << ','
                    << metrics.peak << ','
                    << auditionPeak << ','
                    << metrics.centroidHz << ','
                    << metrics.highBandRatio << ','
                    << metrics.stickingFraction << ','
                    << metrics.meanGripUtilization << ','
                    << metrics.maxContactTemperatureC << '\n';

                if (!metrics.finite
                    || auditionPeak >= 0.98
                    || metrics.maxContactTemperatureC > 85.1
                    || metrics.stickingFraction < 0.0
                    || metrics.stickingFraction > 1.0)
                {
                    std::cerr
                        << "FAIL control point"
                        << " position=" << positions[z]
                        << " pressure=" << pressures[p]
                        << " speed=" << speeds[s]
                        << " rms=" << metrics.rms
                        << " audition_peak=" << auditionPeak
                        << " temp=" << metrics.maxContactTemperatureC
                        << " sticking=" << metrics.stickingFraction
                        << '\n';
                    ok = false;
                }
            }
        }
    }

    for (std::size_t z = 0; z < positions.size(); ++z)
    {
        for (std::size_t p = 0; p < pressures.size(); ++p)
        {
            for (std::size_t s = 0; s < speeds.size(); ++s)
            {
                const auto& a = grids[z][p][s];
                const auto compare = [&](const Metrics& b)
                {
                    if (a.rms > 1.0e-6 && b.rms > 1.0e-6)
                    {
                        maxAdjacentRmsJumpDb = std::max(
                            maxAdjacentRmsJumpDb,
                            std::abs(dbRatio(a.rms, b.rms)));
                    }
                    if (a.centroidHz > 50.0 && b.centroidHz > 50.0)
                    {
                        const auto ratio =
                            std::max(a.centroidHz, b.centroidHz)
                            / std::min(a.centroidHz, b.centroidHz);
                        maxAdjacentCentroidRatio =
                            std::max(maxAdjacentCentroidRatio, ratio);
                    }
                };

                if (p + 1 < pressures.size())
                    compare(grids[z][p + 1][s]);
                if (s + 1 < speeds.size())
                    compare(grids[z][p][s + 1]);
            }
        }
    }

    std::cout
        << "control_surface_max_audition_peak=" << maxAuditionPeak << '\n'
        << "control_surface_max_adjacent_rms_jump_db="
        << maxAdjacentRmsJumpDb << '\n'
        << "control_surface_max_adjacent_centroid_ratio="
        << maxAdjacentCentroidRatio << '\n';

    // These are intentionally broad first-line guards. The CSV is also
    // inspected for shape/monotonicity so thresholds can be tightened around
    // the actual physical operating surface instead of guessing from one tone.
    if (maxAdjacentRmsJumpDb > 18.0)
    {
        std::cerr << "FAIL: control surface has an abrupt adjacent RMS cliff\n";
        ok = false;
    }
    if (maxAdjacentCentroidRatio > 3.5)
    {
        std::cerr << "FAIL: control surface has an abrupt adjacent timbre cliff\n";
        ok = false;
    }

    if (!ok)
        return EXIT_FAILURE;

    std::cout << "PASS control surface regression\n";
    return EXIT_SUCCESS;
}
