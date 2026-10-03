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
    double state = 1.0;
    bool sticking = false;

    void reset() noexcept
    {
        state = 1.0;
        sticking = false;
    }

    double solve(double incomingVelocity,
                 double bowVelocity,
                 double normalForce,
                 double stringImpedance,
                 double sampleRate,
                 double staticGripScale = 1.0,
                 double slidingGripScale = 1.0,
                 double stateRateScale = 1.0) noexcept
    {
        state = std::clamp(state, 0.20, 1.35);
        const auto requiredForce = 2.0 * stringImpedance * (bowVelocity - incomingVelocity);
        const auto staticLimit = 1.2 * staticGripScale * normalForce
                               * std::clamp(state, 0.45, 1.15);

        if (std::abs(requiredForce) <= staticLimit)
        {
            sticking = true;
            return bowVelocity;
        }

        sticking = false;
        const bool positive = requiredForce > 0.0;
        double lo = positive ? bowVelocity - 3.0 : bowVelocity + 1.0e-10;
        double hi = positive ? bowVelocity - 1.0e-10 : bowVelocity + 3.0;

        const auto equation = [&](double stringVelocity) noexcept
        {
            const auto friction = slidingGripScale * normalForce
                                * muJump(stringVelocity - bowVelocity) * state;
            return 2.0 * stringImpedance * (stringVelocity - incomingVelocity)
                 + (positive ? -friction : friction);
        };

        auto glo = equation(lo);
        const auto ghi = equation(hi);

        if (glo * ghi > 0.0)
        {
            const auto force = positive ? staticLimit : -staticLimit;
            return incomingVelocity + force / (2.0 * stringImpedance);
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
        const auto frictionForce = 2.0 * stringImpedance * (stringVelocity - incomingVelocity);
        const auto slip = std::abs(stringVelocity - bowVelocity);

        double stateTarget = 1.0;
        double timeConstant = 0.0045;
        if (slip >= 1.0e-5)
        {
            stateTarget = muSteady(slip) / muJump(slip);
            const auto frictionPower = std::abs(frictionForce * (stringVelocity - bowVelocity));
            timeConstant = 0.0018 / (1.0 + 18.0 * frictionPower) + 0.00025;
        }

        const auto stateAlpha = 1.0 - std::exp(
            -stateRateScale / (sampleRate * timeConstant));
        state += stateAlpha * (stateTarget - state);
        state = std::clamp(state, 0.20, 1.35);
        return stringVelocity;
    }
};
} // namespace fiddle::detail
