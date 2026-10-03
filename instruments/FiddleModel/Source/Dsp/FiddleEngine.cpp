#include "FiddleEngine.h"

#include "detail/BodyModel.h"
#include "detail/BowContact.h"
#include "detail/ModelConstants.h"
#include "detail/WaveguidePrimitives.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fiddle
{
using namespace detail;

struct FiddleEngine::Impl
{
    double sampleRate = 48000.0;

    std::array<DelayRail, stringCount> toBridge{};
    std::array<DelayRail, stringCount> fromBridge{};
    std::array<DelayRail, stringCount> toNut{};
    std::array<DelayRail, stringCount> fromNut{};

    std::array<double, stringCount> lossX1{};
    std::array<double, stringCount> allpassX1{};
    std::array<double, stringCount> allpassY1{};
    std::array<double, stringCount> filterPhaseDelay{};
    std::array<BowContact, stringCount> contacts{};

    ModalBank body{};
    RadiationFilter radiation{};

    Smoother pressure{};
    Smoother speed{};
    Smoother attack{};
    Smoother position{};
    Smoother balance{};
    Smoother gate{};
    std::array<Smoother, stringCount> speakingFrequency{};

    Controls controlTargets{};
    DebugState debug{};

    double velocityScale = 1.0;
    double bowSpeed = 0.0;
    int primaryString = 1;
    int pairLower = 1;

    void prepare(double newSampleRate)
    {
        sampleRate = std::clamp(newSampleRate, 32000.0, 192000.0);
        body.prepare(sampleRate);
        radiation.prepare(sampleRate);

        pressure.prepare(sampleRate, 0.012);
        speed.prepare(sampleRate, 0.012);
        attack.prepare(sampleRate, 0.020);
        position.prepare(sampleRate, 0.015);
        balance.prepare(sampleRate, 0.015);
        gate.prepare(sampleRate, 0.018);

        for (std::size_t i = 0; i < speakingFrequency.size(); ++i)
        {
            speakingFrequency[i].prepare(sampleRate, 0.018);
            filterPhaseDelay[i] = reflectionPhaseDelaySamples(
                sampleRate, openFrequency[i], lossGain[i], lossAlpha[i], allpassA[i]);
        }
        reset();
    }

    void reset()
    {
        for (auto& rail : toBridge) rail.clear();
        for (auto& rail : fromBridge) rail.clear();
        for (auto& rail : toNut) rail.clear();
        for (auto& rail : fromNut) rail.clear();
        for (auto& contact : contacts) contact.reset();

        lossX1.fill(0.0);
        allpassX1.fill(0.0);
        allpassY1.fill(0.0);
        body.reset();
        radiation.reset();

        pressure.reset(controlTargets.pressure);
        speed.reset(controlTargets.speed);
        attack.reset(controlTargets.attack);
        position.reset(controlTargets.position);
        balance.reset(controlTargets.balance);
        gate.reset(0.0);

        for (std::size_t i = 0; i < speakingFrequency.size(); ++i)
            speakingFrequency[i].reset(openFrequency[i]);

        velocityScale = 1.0;
        bowSpeed = 0.0;
        primaryString = 1;
        pairLower = 1;
        debug = {};
    }

    void setControls(const Controls& controls) noexcept
    {
        controlTargets.pressure = static_cast<float>(clamp01(controls.pressure));
        controlTargets.speed = static_cast<float>(clamp01(controls.speed));
        controlTargets.attack = static_cast<float>(clamp01(controls.attack));
        controlTargets.position = static_cast<float>(clamp01(controls.position));
        controlTargets.balance = std::clamp(controls.balance, -1.0f, 1.0f);

        pressure.setTarget(controlTargets.pressure);
        speed.setTarget(controlTargets.speed);
        attack.setTarget(controlTargets.attack);
        position.setTarget(controlTargets.position);
        balance.setTarget(controlTargets.balance);
    }

    void noteOn(double frequencyHz, double velocity)
    {
        const auto requested = std::clamp(frequencyHz, openFrequency.front(), 2500.0);
        primaryString = choosePrimaryString(requested);
        pairLower = std::clamp(primaryString, 0, 2);

        for (std::size_t i = 0; i < speakingFrequency.size(); ++i)
            speakingFrequency[i].setTarget(openFrequency[i]);

        const auto primary = static_cast<std::size_t>(primaryString);
        speakingFrequency[primary].setTarget(std::max(requested, openFrequency[primary]));

        velocityScale = 0.35 + 0.65 * clamp01(velocity);
        gate.setTarget(1.0);
    }

    void noteOff() noexcept
    {
        gate.setTarget(0.0);
    }

    static double balanceToLocalBowAngle(double balanceValue) noexcept
    {
        // 7th-order fit to the validated v0.4c Balance->Bow Angle calibration.
        // Maximum fit error over [-1,1] is about 0.013 degree.
        const auto x = std::clamp(balanceValue, -1.0, 1.0);
        constexpr std::array<double, 8> c {
            1.15679654, -0.45509499, -3.56088352, 1.08519450,
            5.32921783, -1.02536461, -7.45033364, 0.48225702
        };
        double y = c[0];
        for (std::size_t i = 1; i < c.size(); ++i)
            y = y * x + c[i];
        return y;
    }

    std::array<double, stringCount> makeBowForces(double totalForce, double balanceValue) noexcept
    {
        std::array<double, stringCount> result{};
        if (totalForce <= 0.0)
        {
            debug.bowAngleDeg = static_cast<float>(
                pairChordAngleDeg[static_cast<std::size_t>(pairLower)]
                + balanceToLocalBowAngle(balanceValue));
            return result;
        }

        const auto baseAngle = pairChordAngleDeg[static_cast<std::size_t>(pairLower)];
        const auto angleDeg = baseAngle + balanceToLocalBowAngle(balanceValue);
        debug.bowAngleDeg = static_cast<float>(angleDeg);

        const auto slope = std::tan(angleDeg * pi / 180.0);
        std::array<double, stringCount> gaps{};
        double highest = -1.0e30;
        for (std::size_t i = 0; i < stringCount; ++i)
        {
            gaps[i] = bridgeYmm[i] - slope * bridgeXmm[i];
            highest = std::max(highest, gaps[i]);
        }
        for (auto& gap : gaps)
            gap = highest - gap;

        const auto forceAtDepth = [&](double depth) noexcept
        {
            double sum = 0.0;
            for (const auto gap : gaps)
            {
                const auto indentation = std::max(0.0, depth - gap);
                sum += contactStiffness * std::pow(indentation, contactExponent);
            }
            return sum;
        };

        double lo = 0.0;
        double hi = 2.0;
        while (forceAtDepth(hi) < totalForce && hi < 16.0)
            hi *= 2.0;

        for (int iteration = 0; iteration < 22; ++iteration)
        {
            const auto mid = 0.5 * (lo + hi);
            if (forceAtDepth(mid) < totalForce)
                lo = mid;
            else
                hi = mid;
        }

        const auto depth = 0.5 * (lo + hi);
        for (std::size_t i = 0; i < stringCount; ++i)
        {
            const auto indentation = std::max(0.0, depth - gaps[i]);
            result[i] = contactStiffness * std::pow(indentation, contactExponent);
            if (result[i] < 1.0e-9)
                result[i] = 0.0;
        }
        return result;
    }

    double processSample() noexcept
    {
        const auto p = pressure.next();
        const auto s = speed.next();
        const auto a = attack.next();
        const auto pos = position.next();
        const auto bal = balance.next();
        const auto gateValue = gate.next();

        const auto bowTargetSpeed = 0.04 + 0.61 * std::pow(s, 1.25);
        const auto bowAcceleration = 0.25 * std::pow(12.0, a);
        const auto totalForce = (0.06 * std::pow(8.0, p)) * velocityScale * gateValue;
        const auto beta = 0.22 + (0.06 - 0.22) * pos;

        const auto desiredSpeed = bowTargetSpeed * gateValue;
        const auto maxDelta = bowAcceleration / sampleRate;
        bowSpeed += std::clamp(desiredSpeed - bowSpeed, -maxDelta, maxDelta);

        std::array<double, stringCount> currentFrequency{};
        std::array<double, stringCount> bridgeDelay{};
        std::array<double, stringCount> nutDelay{};

        for (std::size_t i = 0; i < currentFrequency.size(); ++i)
        {
            currentFrequency[i] = std::max(20.0, speakingFrequency[i].next());
            auto oneWay = sampleRate / (2.0 * currentFrequency[i]) - 0.5 * filterPhaseDelay[i];
            oneWay = std::clamp(oneWay, 4.0, static_cast<double>(delaySize - 8));
            bridgeDelay[i] = std::max(1.2, oneWay * beta);
            nutDelay[i] = std::max(1.2, oneWay * (1.0 - beta));
        }

        std::array<double, stringCount> incidentBridge{};
        std::array<double, stringCount> incidentNut{};
        std::array<double, stringCount> incomingBridge{};
        std::array<double, stringCount> incomingNut{};

        double incidentForce = 0.0;
        double impedanceSum = 0.0;
        for (std::size_t i = 0; i < stringCount; ++i)
        {
            incidentBridge[i] = toBridge[i].read(bridgeDelay[i]);
            incidentNut[i] = toNut[i].read(nutDelay[i]);
            incomingBridge[i] = fromBridge[i].read(bridgeDelay[i]);
            incomingNut[i] = fromNut[i].read(nutDelay[i]);
            incidentForce += 2.0 * stringImpedance[i] * incidentBridge[i];
            impedanceSum += stringImpedance[i];
        }

        const auto direct = body.direct();
        const auto bridgeVelocity = (body.knownPart() + direct * incidentForce)
                                  / (1.0 + direct * impedanceSum);
        body.push(incidentForce - impedanceSum * bridgeVelocity);

        const auto bowForce = makeBowForces(totalForce, bal);
        debug.contactNormalForceN.fill(0.0f);
        debug.sticking.fill(false);

        for (std::size_t i = 0; i < stringCount; ++i)
        {
            const auto reflectedBridge = bridgeVelocity - incidentBridge[i];
            const auto lossFiltered = lossGain[i]
                * ((1.0 - lossAlpha[i]) * incidentNut[i] + lossAlpha[i] * lossX1[i]);
            lossX1[i] = incidentNut[i];

            const auto filtered = allpassA[i] * lossFiltered
                                + allpassX1[i]
                                - allpassA[i] * allpassY1[i];
            allpassX1[i] = lossFiltered;
            allpassY1[i] = filtered;
            const auto reflectedNut = -filtered;

            const auto incomingVelocity = incomingBridge[i] + incomingNut[i];
            double injection = 0.0;
            if (bowForce[i] > 1.0e-8 && bowSpeed > 1.0e-8)
            {
                const auto stringVelocity = contacts[i].solve(
                    incomingVelocity, bowSpeed, bowForce[i], stringImpedance[i], sampleRate);
                injection = stringVelocity - incomingVelocity;
            }
            else
            {
                contacts[i].sticking = false;
            }

            toBridge[i].write(incomingNut[i] + injection);
            toNut[i].write(incomingBridge[i] + injection);
            fromBridge[i].write(reflectedBridge);
            fromNut[i].write(reflectedNut);

            debug.contactNormalForceN[i] = static_cast<float>(bowForce[i]);
            debug.speakingFrequencyHz[i] = static_cast<float>(currentFrequency[i]);
            debug.sticking[i] = contacts[i].sticking;
        }

        for (std::size_t i = 0; i < stringCount; ++i)
        {
            toBridge[i].advance();
            toNut[i].advance();
            fromBridge[i].advance();
            fromNut[i].advance();
        }

        debug.bowSpeedMps = static_cast<float>(bowSpeed);
        debug.bridgeVelocity = static_cast<float>(bridgeVelocity);
        debug.bowPairLowerString = pairLower;
        debug.primaryString = primaryString;

        // Listening/output calibration only; not part of the mechanical closure.
        return 18.0 * radiation.process(bridgeVelocity);
    }
};

FiddleEngine::FiddleEngine() : impl_(std::make_unique<Impl>()) {}
FiddleEngine::~FiddleEngine() = default;

void FiddleEngine::prepare(double sampleRate) { impl_->prepare(sampleRate); }
void FiddleEngine::reset() { impl_->reset(); }
void FiddleEngine::noteOn(float frequencyHz, float velocity) { impl_->noteOn(frequencyHz, velocity); }
void FiddleEngine::noteOff() { impl_->noteOff(); }
void FiddleEngine::setControls(const Controls& controls) noexcept { impl_->setControls(controls); }

void FiddleEngine::process(float* left, float* right, std::size_t numSamples) noexcept
{
    constexpr float centerGain = 0.70710678f;
    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const auto sample = static_cast<float>(impl_->processSample());
        left[i] += centerGain * sample;
        right[i] += centerGain * sample;
    }
}

DebugState FiddleEngine::debugSnapshot() const noexcept { return impl_->debug; }
} // namespace fiddle
