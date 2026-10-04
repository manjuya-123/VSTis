#pragma once

#include "FiddlePlayLayout.h"

#include <algorithm>

namespace fiddle
{
struct BowGestureProfile
{
    float pressureBoost = 0.0f;
    float speedScale = 1.0f;
    float responseBoost = 0.0f;
    float biteBoost = 0.0f;
    float biteDurationSeconds = 0.008f;
    float durationSeconds = 0.0f;
    float liftDurationSeconds = 0.012f;
    float liftBrake = 2.0f;
    float liftForceCurve = 1.0f;
    float impactVelocityMps = 0.0f;
    float impactDurationSeconds = 0.0f;
    float tremoloReversalsPerSecond = 0.0f;
    float shuffleSubdivisionsPerSecond = 0.0f;
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
            result.biteBoost = 0.03f + 0.04f * strength;
            result.biteDurationSeconds = 0.007f;
            break;

        case BowAction::ShortStroke:
            // A short fiddle stroke needs a prompt stick/catch, not simply
            // more bow speed. Too much speed for the available normal force
            // pushes the contact into slip and actually delays the audible onset.
            result.pressureBoost = 0.10f + 0.08f * strength;
            result.speedScale = 0.90f + 0.40f * strength;
            result.responseBoost = 0.20f + 0.08f * strength;
            result.biteBoost = 0.14f + 0.12f * strength;
            result.biteDurationSeconds = 0.007f;
            result.durationSeconds = 0.095f - 0.040f * strength;
            // Short stroke leaves the string with a rounded wrist lift:
            // contact pressure falls over ~12-14 ms and bow travel brakes gently.
            result.liftDurationSeconds = 0.014f - 0.003f * strength;
            result.liftBrake = 1.8f + 0.8f * strength;
            result.liftForceCurve = 0.72f;
            result.oneShot = true;
            break;

        case BowAction::Tremolo:
            // Tremolo is a light, fast, even wrist gesture. Keep normal force
            // relatively modest while raising bow speed/response and reversal rate.
            result.pressureBoost = 0.05f * strength;
            result.speedScale = 1.02f + 0.48f * strength;
            result.responseBoost = 0.22f + 0.10f * strength;
            result.biteBoost = 0.06f + 0.06f * strength;
            result.biteDurationSeconds = 0.0045f;
            result.tremoloReversalsPerSecond = 11.0f + 11.0f * strength;
            break;

        case BowAction::Shuffle:
            // Shuffle needs a stronger grounded catch and more pulse contrast
            // than Tremolo. Rhythm comes from the physical long-short-short cell.
            result.pressureBoost = 0.08f + 0.11f * strength;
            result.speedScale = 0.80f + 0.30f * strength;
            result.responseBoost = 0.12f + 0.08f * strength;
            result.biteBoost = 0.12f + 0.12f * strength;
            result.biteDurationSeconds = 0.0065f;
            result.shuffleSubdivisionsPerSecond = 8.5f + 6.5f * strength;
            break;

        case BowAction::DroneBow:
            result.pressureBoost = gesturePressure;
            result.speedScale = 0.88f + 0.32f * strength;
            result.balancedPair = true;
            break;

        case BowAction::AccentStroke:
            result.pressureBoost = 0.12f + 0.12f * strength;
            result.speedScale = 0.90f + 0.42f * strength;
            result.responseBoost = 0.18f + 0.10f * strength;
            result.biteBoost = 0.18f + 0.20f * strength;
            result.biteDurationSeconds = 0.009f;
            result.durationSeconds = 0.070f - 0.025f * strength;
            // Accent bites hard, then the hand gets out of the string quickly.
            // A short, strongly braked lift preserves the string/body ring while
            // making the bow-contact release audibly more abrupt than Short.
            result.liftDurationSeconds = 0.0065f - 0.0015f * strength;
            result.liftBrake = 4.0f + 1.5f * strength;
            result.liftForceCurve = 1.85f;
            result.oneShot = true;
            break;

        case BowAction::Chop:
            result.pressureBoost = 0.28f + 0.12f * strength;
            result.speedScale = 0.20f + 0.16f * strength;
            result.responseBoost = 0.14f + 0.06f * strength;
            result.biteBoost = 0.30f + 0.22f * strength;
            result.biteDurationSeconds = 0.006f;
            result.durationSeconds = 0.042f - 0.016f * strength;
            result.liftDurationSeconds = 0.0045f;
            result.liftBrake = 6.0f;
            result.liftForceCurve = 2.4f;
            // A chop is a collision at the bowing point, not merely a very
            // short sustained bow. Velocity controls the transverse impact.
            result.impactVelocityMps = 0.008f + 0.009f * strength;
            result.impactDurationSeconds = 0.0026f - 0.0006f * strength;
            result.oneShot = true;
            break;

        case BowAction::Release:
        case BowAction::None:
            break;
    }

    return result;
}
} // namespace fiddle
