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

    // A bow-hair ribbon begins to lose individual gripping hairs before the
    // entire patch reaches its bulk static limit. Far from yield it must
    // impose ideal stick, while just below yield it must show finite slip.
    BowContact distributedHair;
    distributedHair.temperatureC = BowContact::crossingTemperatureC;
    constexpr double bowSpeed = 0.35;
    constexpr double impedance = 0.25;
    constexpr double normalForce = 0.30;
    const auto staticLimit = 1.2 * normalForce;
    const auto wellBelowYield = distributedHair.solve(
        bowSpeed - 0.75 * staticLimit / (2.0 * impedance),
        bowSpeed, normalForce, impedance, sampleRate);
    if (std::abs(wellBelowYield - bowSpeed) > 1.0e-8)
        return fail("hair below yield failed to stick to the bow");

    distributedHair.reset();
    distributedHair.temperatureC = BowContact::crossingTemperatureC;
    const auto partialVelocity = distributedHair.solve(
        bowSpeed - 0.94 * staticLimit / (2.0 * impedance),
        bowSpeed, normalForce, impedance, sampleRate);
    if (!(partialVelocity < bowSpeed - 1.0e-4
          && partialVelocity > bowSpeed - 0.94 * staticLimit / (2.0 * impedance)))
        return fail("partial-slip band did not reduce bow grip continuously");
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
