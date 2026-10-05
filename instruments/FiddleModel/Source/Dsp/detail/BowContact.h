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
    static constexpr double crossingTemperatureC = 46.2;

    double temperatureC = ambientTemperatureC;
    double lastSlipSpeedMps = 0.0;
    double lastGripUtilization = 0.0;
    bool sticking = false;

    void reset() noexcept
    {
        temperatureC = ambientTemperatureC;
        lastSlipSpeedMps = 0.0;
        lastGripUtilization = 0.0;
        sticking = false;
    }

    [[nodiscard]] static double reducedYield(double temperature) noexcept
    {
        // Smooth reduced-order surrogate for Y(T). The full paper derives Y(T)
        // from a thermal control-volume model; we retain one scalar contact
        // temperature for real-time use.
        return 1.5 + 4.0
            / (1.0 + std::exp((temperature - 48.0) / 7.5));
    }

    [[nodiscard]] static double referenceYield() noexcept
    {
        // Eq. (10) in Woodhouse & Galluzzo fixes K*Y(T0)=1 at the crossing
        // between mu_steady and mu_jump. Their normal-bow fit gives T0=46.2 C.
        return reducedYield(crossingTemperatureC);
    }

    [[nodiscard]] double rosinStrengthScale() const noexcept
    {
        return std::clamp(
            reducedYield(temperatureC) / referenceYield(), 0.35, 1.45);
    }

    [[nodiscard]] static double steadyCalibratedTemperatureC(double slipSpeed) noexcept
    {
        const auto speed = std::max(std::abs(slipSpeed), 1.0e-6);
        const auto desiredScale = std::clamp(
            muSteady(speed) / muJump(speed), 0.36, 1.44);

        // Invert reducedYield(T)/Y(T0)=desiredScale. This makes the
        // reduced-order steady state reproduce the published mu_steady fit
        // while preserving mu_jump as the instantaneous rate term.
        const auto desiredYield = desiredScale * referenceYield();
        const auto q = std::clamp(
            (desiredYield - 1.5) / 4.0, 1.0e-5, 1.0 - 1.0e-5);
        const auto temperature =
            48.0 + 7.5 * std::log(1.0 / q - 1.0);

        return std::clamp(
            temperature, ambientTemperatureC, ambientTemperatureC + 65.0);
    }

    [[nodiscard]] double contactTemperatureC() const noexcept
    {
        return temperatureC;
    }

    [[nodiscard]] double slipSpeedMps() const noexcept
    {
        return lastSlipSpeedMps;
    }

    [[nodiscard]] double gripUtilization() const noexcept
    {
        return lastGripUtilization;
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
            // Calibrate the reduced-order thermal state from the two published
            // friction fits: in steady sliding, mu_jump(v)*Y(T)/Y(T0) should
            // reproduce mu_steady(v).
            targetTemperature = steadyCalibratedTemperatureC(speed);

            // Frictional work controls how quickly the contact approaches that
            // calibrated steady state. These time constants remain a real-time
            // surrogate, not literal rosin thermal constants.
            timeConstant =
                0.0016 / (1.0 + 14.0 * std::abs(frictionPower)) + 0.00035;
        }

        const auto alpha = 1.0 - std::exp(
            -stateRateScale / (sampleRate * timeConstant));
        temperatureC += alpha * (targetTemperature - temperatureC);
        temperatureC = std::clamp(
            temperatureC, ambientTemperatureC, ambientTemperatureC + 65.0);
    }

    void relax(double sampleRate, double stateRateScale = 1.0) noexcept
    {
        sticking = false;
        lastSlipSpeedMps = 0.0;
        lastGripUtilization = 0.0;
        updateTemperature(0.0, 0.0, sampleRate, stateRateScale);
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
        lastGripUtilization = std::clamp(
            std::abs(requiredForce) / (staticLimit + 1.0e-12),
            0.0, 3.0);

        if (std::abs(requiredForce) <= staticLimit)
        {
            sticking = true;
            lastSlipSpeedMps = 0.0;
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
            lastSlipSpeedMps = slip;
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
        lastSlipSpeedMps = slip;

        updateTemperature(
            slip,
            std::abs(frictionForce * slip),
            sampleRate,
            stateRateScale);

        return stringVelocity;
    }
};
} // namespace fiddle::detail
