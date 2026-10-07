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

    // At 44.1/48 kHz the sliding branch should respond over a finite
    // hair/rosin microcontact time without changing rigid stick calibration.
    for (const auto rate : { 44100.0, 48000.0 })
    {
        BowContact sticking;
        const auto gripVelocity = sticking.solve(
            0.0, 0.20, 0.60, 0.24, rate);
        if (!sticking.sticking || std::abs(gripVelocity - 0.20) > 1.0e-10)
            return fail("slip relaxation changed calibrated sticking speed");

        BowContact slipping;
        const auto firstVelocity = slipping.softenSlidingForce(
            0.0, 0.10, 0.24, rate);
        if (!(firstVelocity > 0.0
              && firstVelocity < 0.10 / 0.48))
            return fail("rosin sliding contact lacks finite force response");
        double settledVelocity = firstVelocity;
        for (int i = 0; i < 150; ++i)
            settledVelocity = slipping.softenSlidingForce(
                0.0, 0.10, 0.24, rate);
        if (!std::isfinite(settledVelocity)
            || std::abs(settledVelocity - 0.10 / 0.48) > 1.0e-8)
            return fail("rosin sliding force failed to reach physical target");
        for (int i = 0; i < 1000; ++i)
            slipping.relax(rate);
        if (std::abs(slipping.slidingForceN) > 1.0e-8)
            return fail("rosin contact retained force after bow lift");
    }

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
