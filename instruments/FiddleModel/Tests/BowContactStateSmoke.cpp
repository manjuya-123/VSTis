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

    // The recapture interpolation must preserve the existing friction force
    // exactly when disabled, and its enabled result must stay bounded by
    // static grip across one sliding-to-stick transition.
    BowContact withoutRecatch;
    withoutRecatch.sticking = false;
    withoutRecatch.lastGripUtilization = 1.50;
    withoutRecatch.lastFrictionForceN = 0.090;
    BowContact withRecatch = withoutRecatch;
    constexpr double incoming = 0.025;
    constexpr double bow = 0.120;
    constexpr double impedance = 0.19;
    constexpr double normal = 0.08;
    const auto standardVelocity = withoutRecatch.solve(
        incoming, bow, normal, impedance, sampleRate);
    const auto correctedVelocity = withRecatch.solve(
        incoming, bow, normal, impedance, sampleRate,
        1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.12);
    const auto forceBound = 1.2 * normal * 1.08;
    if (!withoutRecatch.sticking || !withRecatch.sticking
        || std::abs(standardVelocity - bow) > 1.0e-12
        || std::abs(correctedVelocity - standardVelocity) < 1.0e-7
        || std::abs(withRecatch.lastFrictionForceN) > forceBound
        || std::abs(correctedVelocity - standardVelocity) > 0.03)
        return fail("sliding-to-stick interpolation lost bounded contact behaviour");

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
