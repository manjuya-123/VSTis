#pragma once

#include "ModelConstants.h"

#include <algorithm>
#include <array>
#include <complex>

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
    explicit ModalBank(bool useRockingModes = false) noexcept
        : rockingSpectrum(useRockingModes)
    {
    }

    std::array<ModalBiquad, bodyModeCount> modes{};
    bool rockingSpectrum = false;
    double sampleRate = 48000.0;
    double frequencyScale = 1.0;
    double dampingScale = 1.0;
    double admittanceScale = 1.0;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        updateCoefficients(true);
    }

    void setMaterialScales(double newFrequencyScale,
                           double newDampingScale,
                           double newAdmittanceScale) noexcept
    {
        frequencyScale = newFrequencyScale;
        dampingScale = newDampingScale;
        admittanceScale = newAdmittanceScale;
        updateCoefficients(false);
    }

    void updateCoefficients(bool resetState) noexcept
    {
        const auto c = 2.0 * sampleRate;
        const auto& definitions =
            rockingSpectrum ? rockingBodyModes : bodyModes;
        for (std::size_t i = 0; i < modes.size(); ++i)
        {
            const auto& def = definitions[i];
            const auto w = 2.0 * pi * def.frequencyHz * frequencyScale;
            const auto zeta = std::max(0.002, def.zeta * dampingScale);
            const auto peak = def.peakAdmittance * admittanceScale;
            const auto g = peak * 2.0 * zeta * w;
            const auto a0 = c * c + 2.0 * zeta * w * c + w * w;
            const auto a1 = -2.0 * c * c + 2.0 * w * w;
            const auto a2 = c * c - 2.0 * zeta * w * c + w * w;
            const auto gc = g * c;

            modes[i].b = { gc / a0, 0.0, -gc / a0 };
            modes[i].a = { 1.0, a1 / a0, a2 / a0 };

            if (resetState)
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

    [[nodiscard]] double currentModalVelocity() const noexcept
    {
        // Current velocity carried by the resonant structural modes only.
        // The separate broadband conductance is useful for mechanical bridge
        // loading, but should not automatically be assumed to radiate with the
        // same efficiency as distributed plate modes.
        double sum = 0.0;
        for (const auto& mode : modes)
            sum += mode.y1;
        return sum;
    }

    [[nodiscard]] double currentRadiatingVelocity() const noexcept
    {
        // Acoustic radiation efficiency belongs to the structural mode shape,
        // not to the bridge admittance itself.  Keep the mechanical modal
        // states untouched and weight only their contribution to the
        // microphone-side radiation path.
        const auto& efficiency =
            rockingSpectrum
                ? rockingRadiationEfficiency
                : bodyRadiationEfficiency;

        double sum = 0.0;
        for (std::size_t i = 0; i < modes.size(); ++i)
            sum += efficiency[i] * modes[i].y1;
        return sum;
    }

    double direct() const noexcept
    {
        // The non-resonant mobility belongs to the same generalized body
        // coordinate as the modal admittance and therefore follows its scale.
        double sum = bodyDirectConductance * admittanceScale;
        for (const auto& mode : modes)
            sum += mode.direct();
        return sum;
    }

    [[nodiscard]] std::complex<double> responseAt(
        double frequencyHz) const noexcept
    {
        const auto omega =
            2.0 * pi * std::max(0.0, frequencyHz) / sampleRate;
        const std::complex<double> z1 {
            std::cos(omega), -std::sin(omega)
        };
        const auto z2 = z1 * z1;

        std::complex<double> sum {
            bodyDirectConductance * admittanceScale, 0.0
        };

        for (const auto& mode : modes)
        {
            const auto numerator =
                mode.b[0] + mode.b[1] * z1 + mode.b[2] * z2;
            const auto denominator =
                1.0 + mode.a[1] * z1 + mode.a[2] * z2;
            sum += numerator / denominator;
        }
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
    double airMix = 0.22;
    double hpX1 = 0.0;
    double hpY1 = 0.0;
    double lpY = 0.0;

    void prepare(double sampleRate,
                 double lowPassHz = 7200.0,
                 double newAirMix = 0.22) noexcept
    {
        const auto dt = 1.0 / sampleRate;
        const auto hpRC = 1.0 / (2.0 * pi * 90.0);
        hpAlpha = hpRC / (hpRC + dt);
        const auto lpRC = 1.0 / (
            2.0 * pi * std::clamp(lowPassHz, 4000.0, 12000.0));
        lpAlpha = dt / (lpRC + dt);
        airMix = std::clamp(newAirMix, 0.0, 0.5);
        reset();
    }

    void reset() noexcept { hpX1 = hpY1 = lpY = 0.0; }

    double process(double x) noexcept
    {
        const auto hp = hpAlpha * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = hp;
        lpY += lpAlpha * (hp - lpY);

        // Preserve some bridge-side air above the body low-pass. Separate
        // left/right instances may use slightly different radiation angles,
        // but both remain derived from the same physical bridge velocity.
        const auto air = hp - lpY;
        return lpY + airMix * air;
    }
};
} // namespace fiddle::detail
