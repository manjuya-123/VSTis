#pragma once

#include "FiddlePlayLayout.h"

#include <algorithm>

namespace fiddle
{
struct BowGestureProfile
{
    float pressureBoost = 0.0f;
    float speedScale = 1.0f;
    float durationSeconds = 0.0f;
    float tremoloReversalsPerSecond = 0.0f;
    bool oneShot = false;
    bool balancedPair = false;
};

inline BowGestureProfile makeBowGestureProfile(BowAction action,
                                               float velocity) noexcept
{
    const auto strength = std::clamp(velocity, 0.0f, 1.0f);
    const auto gesturePressure = 0.24f * (strength - 0.5f);
    const auto gestureSpeed = 0.72f + 0.56f * strength;

    BowGestureProfile result;

    switch (action)
    {
        case BowAction::DownBow:
        case BowAction::UpBow:
            result.pressureBoost = gesturePressure;
            result.speedScale = gestureSpeed;
            break;

        case BowAction::ShortStroke:
            result.pressureBoost = gesturePressure;
            result.speedScale = gestureSpeed;
            result.durationSeconds = 0.095f - 0.040f * strength;
            result.oneShot = true;
            break;

        case BowAction::Tremolo:
            result.pressureBoost = gesturePressure;
            result.speedScale = gestureSpeed;
            result.tremoloReversalsPerSecond = 10.0f + 10.0f * strength;
            break;

        case BowAction::DroneBow:
            result.pressureBoost = gesturePressure;
            result.speedScale = 0.88f + 0.32f * strength;
            result.balancedPair = true;
            break;

        case BowAction::AccentStroke:
            result.pressureBoost = 0.12f + 0.12f * strength;
            result.speedScale = 0.90f + 0.42f * strength;
            result.durationSeconds = 0.070f - 0.025f * strength;
            result.oneShot = true;
            break;

        case BowAction::Chop:
            result.pressureBoost = 0.28f + 0.12f * strength;
            result.speedScale = 0.20f + 0.16f * strength;
            result.durationSeconds = 0.042f - 0.016f * strength;
            result.oneShot = true;
            break;

        case BowAction::Release:
        case BowAction::None:
            break;
    }

    return result;
}
} // namespace fiddle
