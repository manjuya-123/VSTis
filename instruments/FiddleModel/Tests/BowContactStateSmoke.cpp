#include "Dsp/detail/BowContact.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
int fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main()
{
    using fiddle::detail::BowContact;
    using fiddle::detail::muJump;
    using fiddle::detail::muSteady;

    const double speeds[] { 0.01, 0.05, 0.10, 0.40 };
    double previousTemperature = BowContact::ambientTemperatureC;

    for (const auto speed : speeds)
    {
        const auto targetTemperature =
            BowContact::steadyCalibratedTemperatureC(speed);

        if (!(targetTemperature > previousTemperature))
            return fail("steady calibrated contact temperature should rise with slip speed");

        BowContact contact;
        contact.temperatureC = targetTemperature;

        const auto reducedMu =
            muJump(speed) * contact.rosinStrengthScale();
        const auto expectedMu = muSteady(speed);
        const auto relativeError =
            std::abs(reducedMu - expectedMu) / std::max(expectedMu, 1.0e-9);

        if (relativeError > 0.015)
            return fail("thermal rate/state calibration does not reproduce mu_steady");

        previousTemperature = targetTemperature;
    }

    BowContact contact;
    constexpr double sampleRate = 48000.0;

    // A compliant hair ribbon cannot instantaneously impose full bow speed
    // on an initially stationary string. At a large normal load, it should
    // transfer momentum over finite microseconds, then converge during stick.
    BowContact elasticContact;
    constexpr double bowSpeed = 0.20;
    constexpr double stringImpedance = 0.20;
    constexpr double normalForce = 1.0;
    const auto firstVelocity = elasticContact.solve(
        0.0, bowSpeed, normalForce, stringImpedance, sampleRate);
    if (!(firstVelocity > 0.0 && firstVelocity < 0.15))
        return fail("bow-hair boundary is still an instantaneous velocity clamp");
    double latestVelocity = firstVelocity;
    for (int i = 0; i < 100; ++i)
        latestVelocity = elasticContact.solve(
            0.0, bowSpeed, normalForce, stringImpedance, sampleRate);
    if (!elasticContact.sticking || std::abs(latestVelocity - bowSpeed) > 0.005)
        return fail("bow-hair compliance did not converge to bow speed during stick");
    const auto hotTarget = BowContact::steadyCalibratedTemperatureC(0.20);

    for (int i = 0; i < static_cast<int>(0.08 * sampleRate); ++i)
        contact.updateTemperature(0.20, 0.04, sampleRate, 1.0);

    if (std::abs(contact.contactTemperatureC() - hotTarget) > 2.0)
        return fail("contact temperature did not approach calibrated sliding target");

    const auto hotTemperature = contact.contactTemperatureC();

    for (int i = 0; i < static_cast<int>(0.20 * sampleRate); ++i)
        contact.relax(sampleRate);

    if (!(contact.contactTemperatureC() < hotTemperature
          && contact.contactTemperatureC() < 24.0))
        return fail("contact temperature did not cool after sliding stopped");

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
