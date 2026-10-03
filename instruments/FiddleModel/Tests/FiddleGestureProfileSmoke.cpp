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
          && hardShort.oneShot))
        return fail("Short Stroke velocity should make the stroke quicker/crisper");

    const auto softTremolo = makeBowGestureProfile(BowAction::Tremolo, 0.2f);
    const auto hardTremolo = makeBowGestureProfile(BowAction::Tremolo, 0.9f);
    if (!(hardTremolo.tremoloReversalsPerSecond
          > softTremolo.tremoloReversalsPerSecond))
        return fail("Tremolo velocity should increase reversal rate");

    const auto drone = makeBowGestureProfile(BowAction::DroneBow, 0.7f);
    if (!drone.balancedPair)
        return fail("Drone Bow should request a balanced adjacent-string pair");

    const auto chop = makeBowGestureProfile(BowAction::Chop, 0.9f);
    if (!(chop.oneShot && chop.durationSeconds < 0.04f
          && chop.pressureBoost > hardDown.pressureBoost
          && chop.speedScale < softDown.speedScale))
        return fail("Chop should be short, high-force and low-travel");

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
