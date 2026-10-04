#pragma once

#include "ModelConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace fiddle::detail
{
inline double clamp01(double x) noexcept
{
    return std::clamp(x, 0.0, 1.0);
}

struct Smoother
{
    double current = 0.0;
    double target = 0.0;
    double alpha = 1.0;

    void prepare(double sampleRate, double timeSeconds) noexcept
    {
        const auto t = std::max(1.0e-5, timeSeconds);
        alpha = 1.0 - std::exp(-1.0 / (sampleRate * t));
    }

    void reset(double value) noexcept { current = target = value; }
    void setTarget(double value) noexcept { target = value; }

    double next() noexcept
    {
        current += alpha * (target - current);
        return current;
    }
};

struct DelayRail
{
    std::array<double, delaySize> data{};
    int writeIndex = 0;

    void clear() noexcept
    {
        data.fill(0.0);
        writeIndex = 0;
    }

    double read(double delaySamples) const noexcept
    {
        auto pos = static_cast<double>(writeIndex) - delaySamples;
        while (pos < 0.0)
            pos += static_cast<double>(delaySize);

        const auto base = std::floor(pos);
        const auto i0 = static_cast<int>(base) % delaySize;
        const auto wrap = [](int index) noexcept
        {
            while (index < 0)
                index += delaySize;
            while (index >= delaySize)
                index -= delaySize;
            return index;
        };

        const auto im1 = wrap(i0 - 1);
        const auto i1 = wrap(i0 + 1);
        const auto i2 = wrap(i0 + 2);
        const auto frac = pos - base;

        // Four-point third-order Lagrange fractional delay. The earlier
        // two-point linear interpolation was cheap but introduced audible
        // high-frequency loss and position-dependent phase error as the bow
        // split the speaking length into two fractional rails.
        const auto lm1 =
            -frac * (frac - 1.0) * (frac - 2.0) / 6.0;
        const auto l0 =
            (frac + 1.0) * (frac - 1.0) * (frac - 2.0) / 2.0;
        const auto l1 =
            -(frac + 1.0) * frac * (frac - 2.0) / 2.0;
        const auto l2 =
            (frac + 1.0) * frac * (frac - 1.0) / 6.0;

        return lm1 * data[static_cast<std::size_t>(im1)]
             + l0 * data[static_cast<std::size_t>(i0)]
             + l1 * data[static_cast<std::size_t>(i1)]
             + l2 * data[static_cast<std::size_t>(i2)];
    }

    void write(double x) noexcept
    {
        data[static_cast<std::size_t>(writeIndex)] = x;
    }

    void advance() noexcept
    {
        if (++writeIndex >= delaySize)
            writeIndex = 0;
    }
};

inline double reflectionPhaseDelaySamples(double sampleRate,
                                          double frequencyHz,
                                          double reflectionGain,
                                          double reflectionAlpha,
                                          double apA) noexcept
{
    const auto omega = 2.0 * pi * frequencyHz / sampleRate;
    if (omega <= 0.0)
        return 1.0;

    const auto cosW = std::cos(omega);
    const auto sinW = std::sin(omega);

    const auto lossReal = reflectionGain * ((1.0 - reflectionAlpha) + reflectionAlpha * cosW);
    const auto lossImag = reflectionGain * (-reflectionAlpha * sinW);

    const auto numReal = apA + cosW;
    const auto numImag = -sinW;
    const auto denReal = 1.0 + apA * cosW;
    const auto denImag = -apA * sinW;
    const auto denNorm = denReal * denReal + denImag * denImag;

    const auto apReal = (numReal * denReal + numImag * denImag) / denNorm;
    const auto apImag = (numImag * denReal - numReal * denImag) / denNorm;

    const auto real = lossReal * apReal - lossImag * apImag;
    const auto imag = lossReal * apImag + lossImag * apReal;
    auto phase = std::atan2(imag, real);
    if (phase > 0.0)
        phase -= 2.0 * pi;

    return -phase / omega;
}

inline int choosePrimaryString(double frequencyHz) noexcept
{
    int selected = 0;
    for (int i = 0; i < stringCount; ++i)
        if (frequencyHz >= openFrequency[static_cast<std::size_t>(i)])
            selected = i;
    return selected;
}
} // namespace fiddle::detail
