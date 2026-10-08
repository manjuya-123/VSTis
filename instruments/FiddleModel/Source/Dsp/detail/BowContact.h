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
    // Reduced rosin/hair adhesion memory. This is not an elastic force state:
    // it only records how mature the microscopic contact bonds are. Bonds
    // strengthen during stick and are stripped during slip, giving the next
    // catch a small history dependence without adding a second oscillator.
    double adhesionState = 0.5;
    bool sticking = false;
    // Diagnostics: a missing sliding root currently substitutes the static
    // friction limit, which can produce a periodic hard-edged waveform.
    bool usedStaticFallback = false;
    double lastFrictionForceN = 0.0;

    // Reduced torsional/contact degree of freedom. This is deliberately not a
    // second string waveguide: it represents the local surface velocity seen
    // by the bow as twist/contact compliance rings after a friction impulse.
    // The state is strongly damped and only feeds the relative bow/string
    // velocity, so it can perturb slip/re-stick timing without becoming an
    // audible second pitch source.
    double torsionalDisplacement = 0.0;
    double torsionalSurfaceVelocity = 0.0;

    void reset() noexcept
    {
        temperatureC = ambientTemperatureC;
        lastSlipSpeedMps = 0.0;
        lastGripUtilization = 0.0;
        adhesionState = 0.5;
        sticking = false;
        usedStaticFallback = false;
        lastFrictionForceN = 0.0;
        torsionalDisplacement = 0.0;
        torsionalSurfaceVelocity = 0.0;
    }

    [[nodiscard]] double torsionalVelocityMps() const noexcept
    {
        return torsionalSurfaceVelocity;
    }

    void advanceTorsion(double frictionForce,
                        double sampleRate,
                        double coupling) noexcept
    {
        if (sampleRate <= 1.0 || coupling <= 0.0)
            return;

        // A reduced local torsional/contact resonance. The ~1.8 kHz centre is
        // intentionally much faster than the played transverse fundamental,
        // while heavy damping prevents a stable whistle or second oscillator.
        // Coupling is kept small by the engine and scales the force drive, not
        // an arbitrary noise source.
        constexpr double resonanceHz = 1850.0;
        constexpr double dampingRatio = 0.08;
        constexpr double forceToAcceleration = 260.0;
        constexpr double maxSurfaceVelocity = 0.012;

        const auto dt = 1.0 / sampleRate;
        const auto omega = 2.0 * 3.14159265358979323846 * resonanceHz;
        const auto acceleration =
            forceToAcceleration * frictionForce
            - 2.0 * dampingRatio * omega * torsionalSurfaceVelocity
            - omega * omega * torsionalDisplacement;

        // Semi-implicit Euler is stable here because omega*dt remains well
        // below unity throughout the supported sample-rate range.
        torsionalSurfaceVelocity += dt * acceleration;
        torsionalSurfaceVelocity = std::clamp(
            torsionalSurfaceVelocity, -maxSurfaceVelocity, maxSurfaceVelocity);
        torsionalDisplacement += dt * torsionalSurfaceVelocity;
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

    void updateAdhesion(bool isSticking,
                        double slipSpeed,
                        double sampleRate,
                        double stateRateScale) noexcept
    {
        // Rosin junctions form over several milliseconds while the hair is
        // carried with the string, then shed substantially faster once gross
        // sliding begins. Keep the strength excursion deliberately small; the
        // state supplies contact history, not a replacement friction curve.
        const auto speed = std::abs(slipSpeed);
        const auto target = isSticking
            ? 1.0
            : 0.15 + 0.20 * std::exp(-speed / 0.08);
        const auto timeConstant = isSticking ? 0.0045 : 0.0011;
        const auto alpha = 1.0 - std::exp(
            -stateRateScale / (sampleRate * timeConstant));
        adhesionState += alpha * (target - adhesionState);
        adhesionState = std::clamp(adhesionState, 0.0, 1.0);
    }

    void relax(double sampleRate, double stateRateScale = 1.0) noexcept
    {
        sticking = false;
        usedStaticFallback = false;
        lastFrictionForceN = 0.0;
        lastSlipSpeedMps = 0.0;
        lastGripUtilization = 0.0;

        // With the bow lifted, unload contact history toward a neutral state
        // instead of carrying a fully formed or fully stripped junction into
        // the next stroke.
        const auto adhesionAlpha = 1.0 - std::exp(
            -stateRateScale / (sampleRate * 0.008));
        adhesionState += adhesionAlpha * (0.5 - adhesionState);

        // Preserve only a short physical memory when the hair lifts. This is
        // what lets a bow reversal/start inherit the prior contact state rather
        // than hard-resetting to a perfectly periodic orbit.
        advanceTorsion(0.0, sampleRate, 1.0);
        updateTemperature(0.0, 0.0, sampleRate, stateRateScale);
    }

    double solve(double incomingVelocity,
                 double bowVelocity,
                 double normalForce,
                 double characteristicImpedance,
                 double sampleRate,
                 double staticGripScale = 1.0,
                 double slidingGripScale = 1.0,
                 double stateRateScale = 1.0,
                 double adhesionMemoryAmount = 0.0,
                 double torsionalCoupling = 0.0,
                 double externalSurfaceVelocityMps = 0.0,
                 double releaseInterpolationAmount = 0.0) noexcept
    {
        usedStaticFallback = false;
        const auto wasSticking = sticking;
        const auto previousGripUtilization = lastGripUtilization;
        const auto strength = rosinStrengthScale();
        const auto localTorsionalVelocity =
            externalSurfaceVelocityMps
            + torsionalCoupling * torsionalSurfaceVelocity;
        const auto requiredForce =
            2.0 * characteristicImpedance
            * (bowVelocity - localTorsionalVelocity - incomingVelocity);

        // Static grip is less temperature-sensitive than the sliding law in
        // this reduced model, but a hot contact still weakens it somewhat.
        const auto staticStateScale = std::clamp(
            0.85 + 0.15 * strength, 0.78, 1.08);
        const auto adhesionStaticScale =
            1.0
            + adhesionMemoryAmount
                * (0.06 * (adhesionState - 0.5));
        const auto adhesionSlidingScale =
            1.0
            + adhesionMemoryAmount
                * (0.030 * (adhesionState - 0.5));
        const auto staticLimit =
            1.2 * staticGripScale * normalForce
            * staticStateScale * adhesionStaticScale;
        lastGripUtilization = std::clamp(
            std::abs(requiredForce) / (staticLimit + 1.0e-12),
            0.0, 3.0);

        // Audio-rate stick/slip decisions otherwise quantize a release to one
        // exact sample. Estimate where the static threshold was crossed
        // between the previous and current contact solves, then integrate only
        // that threshold-crossing sample between its sticking and sliding
        // solutions. This is a numerical event-time correction, not another
        // oscillator or an arbitrary output crossfade: all following samples
        // use the ordinary nonlinear sliding solve.
        const auto finishSliding =
            [&](double rawStringVelocity,
                double rawFrictionForce,
                double rawSlip) noexcept
        {
            double retainedStickFraction = 0.0;
            if (releaseInterpolationAmount > 0.0
                && wasSticking
                && previousGripUtilization < 1.0
                && lastGripUtilization > 1.0)
            {
                const auto gripSpan =
                    lastGripUtilization - previousGripUtilization;
                if (gripSpan > 1.0e-9)
                {
                    const auto crossingFraction = std::clamp(
                        (1.0 - previousGripUtilization) / gripSpan,
                        0.0, 1.0);
                    retainedStickFraction =
                        std::clamp(releaseInterpolationAmount, 0.0, 1.0)
                        * crossingFraction;
                }
            }

            const auto stickingVelocity =
                bowVelocity - localTorsionalVelocity;
            const auto stringVelocity =
                retainedStickFraction * stickingVelocity
                + (1.0 - retainedStickFraction) * rawStringVelocity;
            const auto effectiveForce =
                2.0 * characteristicImpedance
                * (stringVelocity - incomingVelocity);
            const auto slidingFraction =
                std::max(0.0, 1.0 - retainedStickFraction);

            lastFrictionForceN = effectiveForce;
            lastSlipSpeedMps = rawSlip;
            updateAdhesion(
                false,
                rawSlip,
                sampleRate,
                stateRateScale * slidingFraction);
            updateTemperature(
                rawSlip,
                std::abs(rawFrictionForce * rawSlip),
                sampleRate,
                stateRateScale * slidingFraction);
            advanceTorsion(
                effectiveForce, sampleRate, torsionalCoupling);
            return stringVelocity;
        };

        if (std::abs(requiredForce) <= staticLimit)
        {
            sticking = true;
            lastFrictionForceN = requiredForce;
            lastSlipSpeedMps = 0.0;
            updateAdhesion(true, 0.0, sampleRate, stateRateScale);
            updateTemperature(0.0, 0.0, sampleRate, stateRateScale);
            advanceTorsion(requiredForce, sampleRate, torsionalCoupling);
            return bowVelocity - localTorsionalVelocity;
        }

        sticking = false;
        const bool positive = requiredForce > 0.0;
        double lo = positive ? bowVelocity - 3.0 : bowVelocity + 1.0e-10;
        double hi = positive ? bowVelocity - 1.0e-10 : bowVelocity + 3.0;

        const auto equation = [&](double stringVelocity) noexcept
        {
            const auto friction =
                slidingGripScale * adhesionSlidingScale * normalForce
                * muJump(
                    stringVelocity + localTorsionalVelocity - bowVelocity)
                * rosinStrengthScale();

            return 2.0 * characteristicImpedance
                     * (stringVelocity - incomingVelocity)
                 + (positive ? -friction : friction);
        };

        auto glo = equation(lo);
        const auto ghi = equation(hi);

        if (glo * ghi > 0.0)
        {
            usedStaticFallback = true;
            const auto force = positive ? staticLimit : -staticLimit;
            const auto stringVelocity =
                incomingVelocity + force / (2.0 * characteristicImpedance);
            const auto slip =
                stringVelocity + localTorsionalVelocity - bowVelocity;
            return finishSliding(stringVelocity, force, slip);
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
        const auto slip =
            stringVelocity + localTorsionalVelocity - bowVelocity;
        return finishSliding(stringVelocity, frictionForce, slip);
    }
};
} // namespace fiddle::detail
