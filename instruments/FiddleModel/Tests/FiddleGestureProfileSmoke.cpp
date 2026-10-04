#include "Dsp/FiddleGestureProfile.h"

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
    using fiddle::BowAction;
    using fiddle::makeBowGestureProfile;

    const auto softDown = makeBowGestureProfile(BowAction::DownBow, 0.2f);
    const auto hardDown = makeBowGestureProfile(BowAction::DownBow, 0.9f);
    if (!(hardDown.speedScale > softDown.speedScale
          && hardDown.pressureBoost > softDown.pressureBoost))
        return fail("Down Bow velocity should increase bow energy");

    const auto softShort = makeBowGestureProfile(BowAction::ShortStroke, 0.2f);
    const auto hardShort = makeBowGestureProfile(BowAction::ShortStroke, 0.9f);
    if (!(hardShort.durationSeconds < softShort.durationSeconds
          && hardShort.speedScale > softShort.speedScale
          && hardShort.responseBoost > softShort.responseBoost
          && hardShort.oneShot))
        return fail("Short Stroke velocity should make the stroke quicker/crisper");

    const auto softTremolo = makeBowGestureProfile(BowAction::Tremolo, 0.2f);
    const auto hardTremolo = makeBowGestureProfile(BowAction::Tremolo, 0.9f);
    if (!(hardTremolo.tremoloReversalsPerSecond
          > softTremolo.tremoloReversalsPerSecond
          && hardTremolo.responseBoost > softTremolo.responseBoost))
        return fail("Tremolo velocity should increase reversal rate");

    const auto shuffleSoft = makeBowGestureProfile(BowAction::Shuffle, 0.2f);
    const auto shuffleHard = makeBowGestureProfile(BowAction::Shuffle, 0.9f);
    if (!(shuffleHard.shuffleSubdivisionsPerSecond
          > shuffleSoft.shuffleSubdivisionsPerSecond
          && shuffleHard.speedScale > shuffleSoft.speedScale
          && shuffleHard.responseBoost > shuffleSoft.responseBoost))
        return fail("Shuffle velocity should increase bow subdivision rate and energy");

    if (!(hardTremolo.speedScale > shuffleHard.speedScale
          && hardTremolo.responseBoost > shuffleHard.responseBoost
          && shuffleHard.pressureBoost > hardTremolo.pressureBoost
          && shuffleHard.biteBoost > hardTremolo.biteBoost
          && hardTremolo.tremoloReversalsPerSecond
             > shuffleHard.shuffleSubdivisionsPerSecond))
        return fail("Tremolo and Shuffle must keep distinct physical gesture profiles");

    const auto accent = makeBowGestureProfile(BowAction::AccentStroke, 0.8f);
    if (!(accent.responseBoost > hardDown.responseBoost
          && accent.pressureBoost > hardDown.pressureBoost
          && accent.biteBoost > hardDown.biteBoost))
        return fail("Accent should catch the string faster and harder than ordinary Down Bow");

    if (!(accent.liftDurationSeconds < hardShort.liftDurationSeconds
          && accent.liftBrake > hardShort.liftBrake
          && accent.liftForceCurve > hardShort.liftForceCurve))
        return fail("Accent should leave the string faster and more abruptly than Short Stroke");

    const auto drone = makeBowGestureProfile(BowAction::DroneBow, 0.7f);
    if (!drone.balancedPair)
        return fail("Drone Bow should request a balanced adjacent-string pair");

    const auto softChop = makeBowGestureProfile(BowAction::Chop, 0.2f);
    const auto chop = makeBowGestureProfile(BowAction::Chop, 0.9f);
    if (!(chop.oneShot && chop.durationSeconds < 0.04f
          && chop.pressureBoost > hardDown.pressureBoost
          && chop.speedScale < softDown.speedScale
          && chop.biteBoost > accent.biteBoost
          && chop.impactVelocityMps > softChop.impactVelocityMps
          && chop.impactVelocityMps > 0.0f
          && chop.impactDurationSeconds > 0.0f))
        return fail("Chop should be short, high-force, low-travel, and collision-driven");

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
