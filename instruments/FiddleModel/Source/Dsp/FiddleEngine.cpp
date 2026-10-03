#include "FiddleEngine.h"

#include <algorithm>
#include <cmath>

namespace fiddle
{
namespace { constexpr double twoPi = 6.283185307179586476925286766559; }

void FiddleEngine::prepare(double sampleRate) { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; reset(); }
void FiddleEngine::reset() { phase_ = 0.0; envelope_ = 0.0f; gate_ = false; }
void FiddleEngine::noteOn(float frequencyHz, float velocity) {
    frequencyHz_ = std::max(20.0f, frequencyHz);
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    gate_ = true;
}
void FiddleEngine::noteOff() { gate_ = false; }
void FiddleEngine::setControls(const Controls& controls) noexcept { controls_ = controls; }

void FiddleEngine::process(float* left, float* right, std::size_t numSamples) noexcept
{
    const float attackSeconds = 0.002f + 0.18f * (1.0f - controls_.attack);
    const float releaseSeconds = 0.04f + 0.30f * (1.0f - controls_.speed);
    const float attackCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate_ * attackSeconds)));
    const float releaseCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate_ * releaseSeconds)));
    const double phaseInc = twoPi * static_cast<double>(frequencyHz_) / sampleRate_;
    const float gain = 0.08f + 0.22f * controls_.pressure;
    const float brightness = 0.08f + 0.28f * controls_.position;
    const float pan = std::clamp(controls_.balance, -1.0f, 1.0f);
    const float leftGain = std::sqrt(0.5f * (1.0f - pan));
    const float rightGain = std::sqrt(0.5f * (1.0f + pan));

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float target = gate_ ? velocity_ : 0.0f;
        const float coeff = gate_ ? attackCoeff : releaseCoeff;
        envelope_ += coeff * (target - envelope_);
        const float sample = envelope_ * gain * static_cast<float>(
            std::sin(phase_) + brightness * std::sin(2.0 * phase_) + 0.12 * brightness * std::sin(3.0 * phase_));
        left[i] += sample * leftGain;
        right[i] += sample * rightGain;
        phase_ += phaseInc;
        if (phase_ >= twoPi) phase_ -= twoPi;
    }
}
}
