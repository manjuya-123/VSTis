#pragma once

#include <algorithm>
#include <cmath>

namespace fiddle::detail
{
inline double muSteady(double v) noexcept
{
    return 0.2 * std::pow(std::abs(v) + 0.011, -0.4);
}

inline double muJump(double v) noexcept
{
    return 0.5 * std::pow(std::abs(v) + 0.27, -0.4);
}

struct BowContact
{
    static constexpr double ambientTemperatureC = 20.0;

    double temperatureC = ambientTemperatureC;
    bool sticking = false;

    void reset() noexcept
    {
        temperatureC = ambientTemperatureC;
        sticking = false;
    }

    [[nodiscard]] double rosinStrengthScale() const noexcept
    {
        // Reduced-order fit to the qualitative Y(T) shape in Woodhouse &
        // Galluzzo (2025). This is deliberately not the paper's full thermal
        // control-volume model. The normalization keeps ordinary playing near
        // the previous model while allowing hot rosin to soften.
        const auto yield = 1.5 + 4.0
            / (1.0 + std::exp((temperatureC - 48.0) / 7.5));
        constexpr double referenceYield = 4.05; // around mid-transition
        return std::clamp(yield / referenceYield, 0.35, 1.35);
    }

    [[nodiscard]] double contactTemperatureC() const noexcept
    {
        return temperatureC;
    }

    void updateTemperature(double slip,
                           double frictionPower,
                           double sampleRate,
                           double stateRateScale) noexcept
    {
        const auto speed = std::abs(slip);

        double targetTemperature = ambientTemperatureC;
        double timeConstant = 0.010;

        if (speed > 1.0e-6)
        {
            // Steady contact temperature rises steeply at low sliding speed and
            // then approaches a plateau, matching the qualitative shape of the
            // enhanced-model contact-temperature curve.
            const auto rise = 55.0 * std::sqrt(speed / (speed + 0.15));
            targetTemperature = ambientTemperatureC + rise;

            // Frictional work accelerates heating. The values are a real-time
            // reduced-order calibration, not literal rosin thermal constants.
            timeConstant =
                0.0016 / (1.0 + 14.0 * std::abs(frictionPower)) + 0.00035;
        }

        const auto alpha = 1.0 - std::exp(
            -stateRateScale / (sampleRate * timeConstant));
        temperatureC += alpha * (targetTemperature - temperatureC);
        temperatureC = std::clamp(
            temperatureC, ambientTemperatureC, ambientTemperatureC + 65.0);
    }

    double solve(double incomingVelocity,
                 double bowVelocity,
                 double normalForce,
                 double characteristicImpedance,
                 double sampleRate,
                 double staticGripScale = 1.0,
                 double slidingGripScale = 1.0,
                 double stateRateScale = 1.0) noexcept
    {
        const auto strength = rosinStrengthScale();
        const auto requiredForce =
            2.0 * characteristicImpedance * (bowVelocity - incomingVelocity);

        // Static grip is less temperature-sensitive than the sliding law in
        // this reduced model, but a hot contact still weakens it somewhat.
        const auto staticStateScale = std::clamp(
            0.85 + 0.15 * strength, 0.78, 1.08);
        const auto staticLimit =
            1.2 * staticGripScale * normalForce * staticStateScale;

        if (std::abs(requiredForce) <= staticLimit)
        {
            sticking = true;
            updateTemperature(0.0, 0.0, sampleRate, stateRateScale);
            return bowVelocity;
        }

        sticking = false;
        const bool positive = requiredForce > 0.0;
        double lo = positive ? bowVelocity - 3.0 : bowVelocity + 1.0e-10;
        double hi = positive ? bowVelocity - 1.0e-10 : bowVelocity + 3.0;

        const auto equation = [&](double stringVelocity) noexcept
        {
            const auto friction =
                slidingGripScale * normalForce
                * muJump(stringVelocity - bowVelocity)
                * rosinStrengthScale();

            return 2.0 * characteristicImpedance
                     * (stringVelocity - incomingVelocity)
                 + (positive ? -friction : friction);
        };

        auto glo = equation(lo);
        const auto ghi = equation(hi);

        if (glo * ghi > 0.0)
        {
            const auto force = positive ? staticLimit : -staticLimit;
            const auto stringVelocity =
                incomingVelocity + force / (2.0 * characteristicImpedance);
            const auto slip = stringVelocity - bowVelocity;
            updateTemperature(
                slip, std::abs(force * slip), sampleRate, stateRateScale);
            return stringVelocity;
        }

        for (int iteration = 0; iteration < 12; ++iteration)
        {
            const auto mid = 0.5 * (lo + hi);
            const auto gm = equation(mid);
            if (glo * gm <= 0.0)
                hi = mid;
            else
            {
                lo = mid;
                glo = gm;
            }
        }

        const auto stringVelocity = 0.5 * (lo + hi);
        const auto frictionForce =
            2.0 * characteristicImpedance
            * (stringVelocity - incomingVelocity);
        const auto slip = stringVelocity - bowVelocity;

        updateTemperature(
            slip,
            std::abs(frictionForce * slip),
            sampleRate,
            stateRateScale);

        return stringVelocity;
    }
};
} // namespace fiddle::detail
