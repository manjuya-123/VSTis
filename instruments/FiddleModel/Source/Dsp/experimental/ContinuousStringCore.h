#pragma once

// Experimental, deliberately separate from the legacy FiddleEngine.
// A one-polarisation physical string with continuous displacement history,
// a movable, compliant finger contact and a nonlinear bow-force junction.
// NOT YET a complete violin model: no second polarisation, body, bridge
// mobility, hair dynamics or perceptually validated Helmholtz motion.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace fiddle::experimental
{
class ContinuousStringCore
{
public:
    static constexpr int segments = 128;
    static constexpr double lengthM = 0.328;

    void prepare(double sampleRate, double openFrequencyHz,
                 double tensionN = 45.0) noexcept
    {
        sampleRate_ = std::max(8000.0, sampleRate);
        openHz_ = std::max(40.0, openFrequencyHz);
        tension_ = std::max(1.0, tensionN);
        waveSpeed_ = 2.0 * lengthM * openHz_;
        density_ = tension_ / (waveSpeed_ * waveSpeed_);
        dx_ = lengthM / segments;
        // Oversample until the Courant number stays below unity, including E5.
        substeps_ = std::max(1, static_cast<int>(
            std::ceil(waveSpeed_ / (0.90 * sampleRate_ * dx_))));
        dt_ = 1.0 / (sampleRate_ * substeps_);
        courantSquared_ = std::pow(waveSpeed_ * dt_ / dx_, 2.0);
        nodeMass_ = density_ * dx_;
        reset();
    }

    void reset() noexcept
    {
        previous_.fill(0.0);
        current_.fill(0.0);
        next_.fill(0.0);
        fingerPosition_ = 0.80 * lengthM;
        fingerTargetPosition_ = fingerPosition_;
        fingerLoad_ = 0.0;
        fingerRequested_ = false;
        lastBowForceN_ = 0.0;
        lastFingerForceN_ = 0.0;
        lastSticking_ = false;
    }

    // A note moves a physical contact; it NEVER retunes a delay or erases
    // the existing wave/displacement field. Opening the string releases it.
    void setFingerFrequency(double frequencyHz) noexcept
    {
        fingerRequested_ = frequencyHz > openHz_ * 1.0005;
        if (fingerRequested_)
        {
            const auto speakingLength =
                lengthM * openHz_ / std::max(frequencyHz, openHz_);
            fingerTargetPosition_ =
                std::clamp(speakingLength, 2.0 * dx_, lengthM - 2.0 * dx_);
        }
    }

    void setBowPosition(double fractionFromBridge) noexcept
    {
        bowPosition_ = std::clamp(fractionFromBridge, 0.03, 0.30) * lengthM;
    }

    void setDamping(double perSecond) noexcept
    {
        damping_ = std::max(0.0, perSecond);
    }

    // Optional force-controlled, prescribed bow velocity. No note-on strike.
    // The return is bridge reaction force (not radiated, body-filtered sound).
    double step(double bowVelocityMps, double normalForceN) noexcept
    {
        for (int substep = 0; substep < substeps_; ++substep)
        {
            const auto alpha = 1.0 - std::exp(-dt_ / 0.003);
            fingerLoad_ += alpha
                * ((fingerRequested_ ? 1.0 : 0.0) - fingerLoad_);
            fingerPosition_ += alpha
                * (fingerTargetPosition_ - fingerPosition_);

            for (int j = 1; j < segments; ++j)
            {
                const auto velocityDifference = current_[j] - previous_[j];
                next_[j] = 2.0 * current_[j] - previous_[j]
                    + courantSquared_
                        * (current_[j - 1] - 2.0 * current_[j]
                           + current_[j + 1])
                    - 2.0 * damping_ * dt_ * velocityDifference;
            }
            next_[0] = next_[segments] = 0.0;

            // Implicit spring/dashpot against the fingerboard at a fractional
            // spatial position. The force is solved at the same new time level
            // as the displacement, preventing explicit stiff-spring blowups.
            lastFingerForceN_ = 0.0;
            if (fingerLoad_ > 1.0e-8)
            {
                const auto sample = weights(fingerPosition_);
                const auto pFree = interpolate(next_, sample);
                const auto pOld = interpolate(current_, sample);
                const auto k = 21000.0 * fingerLoad_;
                const auto c = 0.035 * fingerLoad_;
                const auto impulseCoefficient = dt_ * dt_ / nodeMass_;
                const auto magnitude = k * pFree + c * (pFree - pOld) / dt_;
                const auto denominator = 1.0
                    + impulseCoefficient * (k + c / dt_)
                        * sample.squaredWeight();
                lastFingerForceN_ = -magnitude / denominator;
                addForce(sample, lastFingerForceN_, impulseCoefficient);
            }

            // The bow acts on the SAME displacement field as the finger.
            // A stick constraint is applied while the necessary traction is
            // below the static friction limit; sliding force is found by a
            // bracketed implicit monotone solve, never a fabricated pluck.
            lastBowForceN_ = 0.0;
            lastSticking_ = false;
            const auto load = std::max(0.0, normalForceN);
            if (load > 0.0)
            {
                const auto sample = weights(bowPosition_);
                const auto vFree = (interpolate(next_, sample)
                    - interpolate(previous_, sample)) / (2.0 * dt_);
                const auto gamma =
                    dt_ * sample.squaredWeight() / (2.0 * nodeMass_);
                const auto staticLimit = 1.10 * load;
                const auto stickForce = (bowVelocityMps - vFree) / gamma;
                if (std::abs(stickForce) <= staticLimit)
                {
                    lastBowForceN_ = stickForce;
                    lastSticking_ = true;
                }
                else
                {
                    const auto slidingLimit = 0.45 * load;
                    double low = -slidingLimit;
                    double high = slidingLimit;
                    // An implicit dissipative tanh law, with unique root:
                    // F = mu*N*tanh((v_bow - v_string(F))/v_s).
                    for (int iteration = 0; iteration < 24; ++iteration)
                    {
                        const auto mid = 0.5 * (low + high);
                        const auto slip = bowVelocityMps - vFree - gamma * mid;
                        const auto residual =
                            mid - slidingLimit * std::tanh(slip / 0.018);
                        if (residual < 0.0)
                            low = mid;
                        else
                            high = mid;
                    }
                    lastBowForceN_ = 0.5 * (low + high);
                }
                addForce(sample, lastBowForceN_, dt_ * dt_ / nodeMass_);
            }

            previous_.swap(current_);
            current_.swap(next_);
        }
        return tension_ * (current_[1] - current_[0]) / dx_;
    }

    // Discrete midpoint energy for the undamped, unforced wave scheme.
    // When the finger is engaged, the spring's stored energy and the work
    // done by moving the contact must also be accounted for separately.
    [[nodiscard]] double freeStringEnergy() const noexcept
    {
        double energy = 0.0;
        for (int j = 1; j < segments; ++j)
        {
            const auto velocity =
                (current_[j] - previous_[j]) / dt_;
            energy += 0.5 * nodeMass_ * velocity * velocity;
        }
        for (int j = 0; j < segments; ++j)
        {
            energy += tension_ / (2.0 * dx_)
                * (current_[j + 1] - current_[j])
                * (previous_[j + 1] - previous_[j]);
        }
        return energy;
    }

    // Diagnostics only; production musical gestures must not use this.
    void seedFundamentalForTest(double amplitudeM) noexcept
    {
        for (int j = 0; j <= segments; ++j)
        {
            const auto displacement = amplitudeM * std::sin(
                3.14159265358979323846 * j / segments);
            current_[j] = previous_[j] = displacement;
        }
    }

    [[nodiscard]] double displacementAt(double fraction) const noexcept
    {
        return interpolate(current_,
            weights(std::clamp(fraction, 0.0, 1.0) * lengthM));
    }

    [[nodiscard]] double bowForceN() const noexcept { return lastBowForceN_; }
    [[nodiscard]] double fingerForceN() const noexcept { return lastFingerForceN_; }
    [[nodiscard]] bool sticking() const noexcept { return lastSticking_; }
    [[nodiscard]] int internalSubsteps() const noexcept { return substeps_; }
    [[nodiscard]] double courantNumber() const noexcept
    {
        return std::sqrt(courantSquared_);
    }

private:
    struct Sample
    {
        int left = 0;
        double a = 1.0;
        double b = 0.0;
        [[nodiscard]] double squaredWeight() const noexcept
        {
            return a * a + b * b;
        }
    };

    [[nodiscard]] Sample weights(double x) const noexcept
    {
        const auto grid = std::clamp(x / dx_, 1.0,
                                     static_cast<double>(segments - 1));
        const auto left = std::min(static_cast<int>(grid), segments - 2);
        const auto b = grid - left;
        return { left, 1.0 - b, b };
    }

    [[nodiscard]] static double interpolate(
        const std::array<double, segments + 1>& data,
        Sample sample) noexcept
    {
        return sample.a * data[sample.left]
            + sample.b * data[sample.left + 1];
    }

    void addForce(Sample sample, double forceN,
                  double dtSquaredPerMass) noexcept
    {
        next_[sample.left] += dtSquaredPerMass * sample.a * forceN;
        next_[sample.left + 1] += dtSquaredPerMass * sample.b * forceN;
    }

    std::array<double, segments + 1> previous_{};
    std::array<double, segments + 1> current_{};
    std::array<double, segments + 1> next_{};
    double sampleRate_ = 48000.0;
    double openHz_ = 440.0;
    double tension_ = 45.0;
    double waveSpeed_ = 288.64;
    double density_ = 0.00054;
    double dx_ = lengthM / segments;
    double dt_ = 1.0 / 144000.0;
    double nodeMass_ = density_ * dx_;
    double courantSquared_ = 0.0;
    double damping_ = 0.45;
    double bowPosition_ = lengthM * 0.10;
    double fingerPosition_ = lengthM * 0.80;
    double fingerTargetPosition_ = lengthM * 0.80;
    double fingerLoad_ = 0.0;
    bool fingerRequested_ = false;
    int substeps_ = 3;
    double lastBowForceN_ = 0.0;
    double lastFingerForceN_ = 0.0;
    bool lastSticking_ = false;
};
} // namespace fiddle::experimental
