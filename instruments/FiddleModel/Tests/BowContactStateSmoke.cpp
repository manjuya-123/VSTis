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

    // A real bow-hair bundle is not an infinite-stiffness velocity clamp.
    // The first sample must stretch the hairs rather than instantly lock
    // the string to the travelling bow; sustained grip must still converge.
    for (const auto rate : { 44100.0, 48000.0 })
    {
        BowContact hair;
        constexpr double bowVelocity = 0.20;
        constexpr double normalForce = 0.60;
        constexpr double stringImpedance = 0.24;
        const auto firstVelocity = hair.solve(
            0.0, bowVelocity, normalForce, stringImpedance, rate);
        if (!(firstVelocity > 0.0 && firstVelocity < 0.19
              && hair.sticking
              && hair.hairShearDisplacementM > 0.0))
            return fail("bow hair acts like an instantaneous rigid string clamp");

        double settledVelocity = firstVelocity;
        for (int i = 0; i < 200; ++i)
            settledVelocity = hair.solve(
                0.0, bowVelocity, normalForce, stringImpedance, rate);
        if (!(std::isfinite(settledVelocity)
              && std::abs(settledVelocity - bowVelocity) < 0.003
              && hair.sticking))
            return fail("compliant bow hair failed to settle into stable grip");

        const auto reversalVelocity = hair.solve(
            0.0, -bowVelocity, normalForce, stringImpedance, rate);
        if (!(reversalVelocity > -bowVelocity
              && reversalVelocity < bowVelocity
              && std::isfinite(hair.hairShearDisplacementM)))
            return fail("bow reversal bypassed finite hair shear compliance");

        for (int i = 0; i < 1000; ++i)
            hair.relax(rate);
        if (!(std::abs(hair.hairShearDisplacementM) < 1.0e-8))
            return fail("bow hair retained shear while the bow was lifted");
    }

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
