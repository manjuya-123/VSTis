#pragma once

#include "ModelConstants.h"

#include <array>

namespace fiddle::detail
{
struct ModalBiquad
{
    std::array<double, 3> b{};
    std::array<double, 3> a { 1.0, 0.0, 0.0 };
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }

    double knownPart() const noexcept
    {
        return b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2;
    }

    double direct() const noexcept { return b[0]; }

    void push(double x) noexcept
    {
        const auto y = b[0] * x + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
    }
};

struct ModalBank
{
    std::array<ModalBiquad, bodyModeCount> modes{};

    void prepare(double sampleRate) noexcept
    {
        const auto c = 2.0 * sampleRate;
        for (std::size_t i = 0; i < modes.size(); ++i)
        {
            const auto& def = bodyModes[i];
            const auto w = 2.0 * pi * def.frequencyHz;
            const auto g = def.peakAdmittance * 2.0 * def.zeta * w;
            const auto a0 = c * c + 2.0 * def.zeta * w * c + w * w;
            const auto a1 = -2.0 * c * c + 2.0 * w * w;
            const auto a2 = c * c - 2.0 * def.zeta * w * c + w * w;
            const auto gc = g * c;

            modes[i].b = { gc / a0, 0.0, -gc / a0 };
            modes[i].a = { 1.0, a1 / a0, a2 / a0 };
            modes[i].reset();
        }
    }

    void reset() noexcept
    {
        for (auto& mode : modes)
            mode.reset();
    }

    double knownPart() const noexcept
    {
        double sum = 0.0;
        for (const auto& mode : modes)
            sum += mode.knownPart();
        return sum;
    }

    double direct() const noexcept
    {
        double sum = bodyDirectConductance;
        for (const auto& mode : modes)
            sum += mode.direct();
        return sum;
    }

    void push(double force) noexcept
    {
        for (auto& mode : modes)
            mode.push(force);
    }
};

struct RadiationFilter
{
    double hpAlpha = 0.0;
    double lpAlpha = 0.0;
    double hpX1 = 0.0;
    double hpY1 = 0.0;
    double lpY = 0.0;

    void prepare(double sampleRate) noexcept
    {
        const auto dt = 1.0 / sampleRate;
        const auto hpRC = 1.0 / (2.0 * pi * 90.0);
        hpAlpha = hpRC / (hpRC + dt);
        const auto lpRC = 1.0 / (2.0 * pi * 7200.0);
        lpAlpha = dt / (lpRC + dt);
        reset();
    }

    void reset() noexcept { hpX1 = hpY1 = lpY = 0.0; }

    double process(double x) noexcept
    {
        const auto hp = hpAlpha * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = hp;
        lpY += lpAlpha * (hp - lpY);
        return lpY;
    }
};
} // namespace fiddle::detail
