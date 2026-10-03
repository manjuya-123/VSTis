#include "FiddleEngine.h"

#include "detail/BodyModel.h"
#include "detail/BowGeometryMapper.h"
#include "detail/BowContact.h"
#include "detail/ModelConstants.h"
#include "detail/WaveguidePrimitives.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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
    std::array<double, stringCount> runtimeLossGain = lossGain;
    std::array<double, stringCount> runtimeAllpassA = allpassA;
    std::array<double, stringCount> fingerTouch{};
    std::array<BowContact, stringCount> contacts{};

    ModalBank body{};
    RadiationFilter radiation{};
    BowGeometryMapper bowGeometry{};

    Smoother pressure{};
    Smoother speed{};
    Smoother attack{};
    Smoother position{};
    Smoother balance{};
    Smoother vibratoWidth{};
    Smoother vibratoPace{};
    Smoother gate{};
    std::array<Smoother, stringCount> speakingFrequency{};

    Controls controlTargets{};
    MaterialSettings materialSettings{};
    DebugState debug{};

    bool materialConfigured = false;
    double bowResponseScale = 1.0;
    double staticGripScale = 1.0;
    double slidingGripScale = 1.0;
    double contactStateRateScale = 1.0;

    double velocityScale = 1.0;
    double bowSpeed = 0.0;
    double vibratoPhase = 0.0;
    int bowDirection = 1;
    bool bowStrokeStarted = false;
    std::int64_t shortStrokeSamplesRemaining = 0;
    std::int64_t chopDampingSamplesRemaining = 0;
    std::int64_t tremoloSamplesUntilFlip = 0;
    double tremoloReversalsPerSecond = 0.0;
    std::int64_t shuffleSamplesUntilFlip = 0;
    double shuffleSubdivisionsPerSecond = 0.0;
    int shufflePhase = 0;
    double shuffleEnergyScale = 1.0;
    double strokeBiteAmount = 0.0;
    std::int64_t strokeBiteSamplesRemaining = 0;
    std::int64_t strokeBiteTotalSamples = 0;
    int primaryString = 1;
    int pairLower = 1;

    void prepare(double newSampleRate)
    {
        sampleRate = std::clamp(newSampleRate, 32000.0, 192000.0);
        body.prepare(sampleRate);
        radiation.prepare(sampleRate);
        bowGeometry.prepare();
        materialConfigured = false;
        setMaterials(materialSettings);

        pressure.prepare(sampleRate, 0.012);
        speed.prepare(sampleRate, 0.012);
        attack.prepare(sampleRate, 0.020);
        position.prepare(sampleRate, 0.015);
        balance.prepare(sampleRate, 0.015);
        vibratoWidth.prepare(sampleRate, 0.030);
        vibratoPace.prepare(sampleRate, 0.050);
        gate.prepare(sampleRate, 0.018);

        for (std::size_t i = 0; i < speakingFrequency.size(); ++i)
        {
            speakingFrequency[i].prepare(sampleRate, 0.018);
            filterPhaseDelay[i] = reflectionPhaseDelaySamples(
                sampleRate, openFrequency[i], runtimeLossGain[i], lossAlpha[i], runtimeAllpassA[i]);
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
        fingerTouch.fill(0.0);
        allpassY1.fill(0.0);
        body.reset();
        radiation.reset();

        pressure.reset(controlTargets.pressure);
        speed.reset(controlTargets.speed);
        attack.reset(controlTargets.attack);
        position.reset(controlTargets.position);
        balance.reset(controlTargets.balance);
        vibratoWidth.reset(controlTargets.vibratoWidth);
        vibratoPace.reset(controlTargets.vibratoPace);
        gate.reset(0.0);

        for (std::size_t i = 0; i < speakingFrequency.size(); ++i)
            speakingFrequency[i].reset(openFrequency[i]);

        velocityScale = 1.0;
        bowSpeed = 0.0;
        vibratoPhase = 0.0;
        bowDirection = 1;
        bowStrokeStarted = false;
        shortStrokeSamplesRemaining = 0;
        chopDampingSamplesRemaining = 0;
        tremoloSamplesUntilFlip = 0;
        tremoloReversalsPerSecond = 0.0;
        shuffleSamplesUntilFlip = 0;
        shuffleSubdivisionsPerSecond = 0.0;
        shufflePhase = 0;
        shuffleEnergyScale = 1.0;
        strokeBiteAmount = 0.0;
        strokeBiteSamplesRemaining = 0;
        strokeBiteTotalSamples = 0;
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
        controlTargets.vibratoWidth = static_cast<float>(clamp01(controls.vibratoWidth));
        controlTargets.vibratoPace = static_cast<float>(clamp01(controls.vibratoPace));

        pressure.setTarget(controlTargets.pressure);
        speed.setTarget(controlTargets.speed);
        attack.setTarget(controlTargets.attack);
        position.setTarget(controlTargets.position);
        balance.setTarget(controlTargets.balance);
        vibratoWidth.setTarget(controlTargets.vibratoWidth);
        vibratoPace.setTarget(controlTargets.vibratoPace);
    }

    void setMaterials(const MaterialSettings& materials) noexcept
    {
        const bool unchanged =
            materialConfigured
            && materialSettings.body == materials.body
            && materialSettings.bowStick == materials.bowStick
            && materialSettings.contact == materials.contact
            && materialSettings.strings == materials.strings;

        if (unchanged)
            return;

        materialSettings = materials;
        materialConfigured = true;

        switch (materials.body)
        {
            case BodyMaterialPreset::Traditional:
                body.setMaterialScales(1.00, 1.00, 1.00);
                break;
            case BodyMaterialPreset::LightStiffComposite:
                body.setMaterialScales(1.04, 0.82, 1.05);
                break;
            case BodyMaterialPreset::DenseExperimental:
                body.setMaterialScales(0.97, 1.28, 0.90);
                break;
            case BodyMaterialPreset::RigidComposite:
                body.setMaterialScales(1.08, 0.68, 0.96);
                break;
        }

        switch (materials.bowStick)
        {
            case BowStickPreset::PernambucoLike:
                bowResponseScale = 1.00;
                break;
            case BowStickPreset::CarbonLike:
                bowResponseScale = 1.10;
                break;
            case BowStickPreset::LightRigidExperimental:
                bowResponseScale = 1.38;
                break;
            case BowStickPreset::FlexibleExperimental:
                bowResponseScale = 0.76;
                break;
        }

        switch (materials.contact)
        {
            case ContactMaterialPreset::HorsehairMediumRosin:
                staticGripScale = 1.00;
                slidingGripScale = 1.00;
                contactStateRateScale = 1.00;
                break;
            case ContactMaterialPreset::DryLightGrip:
                staticGripScale = 0.86;
                slidingGripScale = 0.90;
                contactStateRateScale = 1.16;
                break;
            case ContactMaterialPreset::HighGripRosin:
                staticGripScale = 1.18;
                slidingGripScale = 1.08;
                contactStateRateScale = 0.84;
                break;
            case ContactMaterialPreset::SyntheticHair:
                staticGripScale = 0.93;
                slidingGripScale = 0.95;
                contactStateRateScale = 1.05;
                break;
        }

        double lossAmountScale = 1.0;
        double dispersionScale = 1.0;
        switch (materials.strings)
        {
            case StringCorePreset::SyntheticCore:
                lossAmountScale = 1.00;
                dispersionScale = 1.00;
                break;
            case StringCorePreset::SteelCore:
                // Quicker, more persistent response: reduce distributed loss and
                // slightly reduce the phase-smearing allpass strength.
                lossAmountScale = 0.72;
                dispersionScale = 0.82;
                break;
            case StringCorePreset::GutLike:
                // A deliberately broad profile for a softer, slower-response core.
                // This is not a calibrated commercial string model.
                lossAmountScale = 1.34;
                dispersionScale = 1.12;
                break;
        }

        for (std::size_t i = 0; i < stringCount; ++i)
        {
            const auto baseLoss = 1.0 - lossGain[i];
            runtimeLossGain[i] = std::clamp(
                1.0 - baseLoss * lossAmountScale, 0.96, 0.99995);
            runtimeAllpassA[i] = std::clamp(
                allpassA[i] * dispersionScale, -0.20, 0.20);

            filterPhaseDelay[i] = reflectionPhaseDelaySamples(
                sampleRate, openFrequency[i],
                runtimeLossGain[i], lossAlpha[i], runtimeAllpassA[i]);
        }
    }

    void beginBowStroke(bool alternateDirection) noexcept
    {
        if (!bowStrokeStarted)
        {
            bowStrokeStarted = true;
            bowDirection = 1;
            return;
        }

        if (alternateDirection)
            bowDirection = -bowDirection;
    }

    void setFingeringLayout(const std::array<float, stringCount>& frequencies,
                            int newPrimaryString,
                            int newPairLower,
                            double velocity)
    {
        primaryString = std::clamp(newPrimaryString, 0, stringCount - 1);
        pairLower = std::clamp(newPairLower, 0, stringCount - 2);

        for (std::size_t i = 0; i < speakingFrequency.size(); ++i)
        {
            const auto requested = frequencies[i] > 0.0f
                ? static_cast<double>(frequencies[i])
                : openFrequency[i];
            const auto target = std::max(requested, openFrequency[i]);

            const bool newlyStopped =
                target > openFrequency[i] * 1.0005
                && std::abs(target - speakingFrequency[i].target) > 0.25;

            speakingFrequency[i].setTarget(target);
            if (newlyStopped)
                fingerTouch[i] = 1.0;
            else if (target <= openFrequency[i] * 1.0005)
                fingerTouch[i] = 0.0;
        }

        velocityScale = 0.35 + 0.65 * clamp01(velocity);
    }

    void noteOn(double frequencyHz, double velocity)
    {
        const auto requested = std::clamp(frequencyHz, openFrequency.front(), 2500.0);
        const auto selectedString = choosePrimaryString(requested);

        std::array<float, stringCount> frequencies{};
        frequencies[static_cast<std::size_t>(selectedString)] =
            static_cast<float>(requested);

        setFingeringLayout(
            frequencies,
            selectedString,
            std::clamp(selectedString, 0, stringCount - 2),
            velocity);

        gate.setTarget(1.0);
    }

    void retune(double frequencyHz) noexcept
    {
        const auto primary = static_cast<std::size_t>(primaryString);
        const auto requested = std::clamp(
            frequencyHz, openFrequency[primary], 2500.0);
        speakingFrequency[primary].setTarget(requested);
    }

    void setStrokeBite(double amount, double durationSeconds) noexcept
    {
        strokeBiteAmount = std::clamp(amount, 0.0, 0.80);
        strokeBiteTotalSamples = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                std::clamp(durationSeconds, 0.001, 0.030) * sampleRate));
        strokeBiteSamplesRemaining = strokeBiteTotalSamples;
    }

    void startBow(int direction) noexcept
    {
        bowStrokeStarted = true;
        bowDirection = direction < 0 ? -1 : 1;
        shortStrokeSamplesRemaining = 0;
        chopDampingSamplesRemaining = 0;
        tremoloSamplesUntilFlip = 0;
        tremoloReversalsPerSecond = 0.0;
        shuffleSamplesUntilFlip = 0;
        shuffleSubdivisionsPerSecond = 0.0;
        shufflePhase = 0;
        shuffleEnergyScale = 1.0;
        gate.setTarget(1.0);
    }

    void startShortStroke(int direction, double durationSeconds) noexcept
    {
        startBow(direction);
        shortStrokeSamplesRemaining = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(durationSeconds * sampleRate));
    }

    void startChop(int direction, double durationSeconds) noexcept
    {
        startShortStroke(direction, durationSeconds);
        chopDampingSamplesRemaining = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                std::max(0.020, durationSeconds + 0.012) * sampleRate));
    }

    void startTremolo(double reversalsPerSecond) noexcept
    {
        bowStrokeStarted = true;
        if (bowDirection == 0)
            bowDirection = 1;

        shortStrokeSamplesRemaining = 0;
        chopDampingSamplesRemaining = 0;
        shuffleSamplesUntilFlip = 0;
        shuffleSubdivisionsPerSecond = 0.0;
        shufflePhase = 0;
        shuffleEnergyScale = 1.0;
        tremoloReversalsPerSecond = std::clamp(reversalsPerSecond, 4.0, 28.0);
        tremoloSamplesUntilFlip = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                sampleRate / tremoloReversalsPerSecond));
        gate.setTarget(1.0);
    }

    void startShuffle(double subdivisionsPerSecond) noexcept
    {
        bowStrokeStarted = true;
        if (bowDirection == 0)
            bowDirection = 1;

        shortStrokeSamplesRemaining = 0;
        chopDampingSamplesRemaining = 0;
        tremoloSamplesUntilFlip = 0;
        tremoloReversalsPerSecond = 0.0;

        shuffleSubdivisionsPerSecond =
            std::clamp(subdivisionsPerSecond, 6.0, 20.0);
        shufflePhase = 0;
        shuffleEnergyScale = 1.12;

        // First stroke is the long member of a long-short-short bowing cell.
        shuffleSamplesUntilFlip = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                2.0 * sampleRate / shuffleSubdivisionsPerSecond));
        gate.setTarget(1.0);
    }

    void stopBow() noexcept
    {
        shortStrokeSamplesRemaining = 0;
        chopDampingSamplesRemaining = 0;
        tremoloSamplesUntilFlip = 0;
        tremoloReversalsPerSecond = 0.0;
        shuffleSamplesUntilFlip = 0;
        shuffleSubdivisionsPerSecond = 0.0;
        shufflePhase = 0;
        shuffleEnergyScale = 1.0;
        gate.setTarget(0.0);
    }

    void noteOff() noexcept
    {
        stopBow();
    }

    std::array<double, stringCount> makeBowForces(double totalForce,
                                                        double balanceValue) noexcept
    {
        const auto geometry = bowGeometry.solve(pairLower, totalForce, balanceValue);
        debug.bowAngleDeg = static_cast<float>(geometry.bowAngleDeg);
        return geometry.normalForceN;
    }

    double processSample() noexcept
    {
        if (shortStrokeSamplesRemaining > 0)
        {
            --shortStrokeSamplesRemaining;
            if (shortStrokeSamplesRemaining == 0)
                gate.setTarget(0.0);
        }

        const bool chopDampingActive = chopDampingSamplesRemaining > 0;
        if (chopDampingSamplesRemaining > 0)
            --chopDampingSamplesRemaining;

        if (tremoloSamplesUntilFlip > 0 && tremoloReversalsPerSecond > 0.0)
        {
            --tremoloSamplesUntilFlip;
            if (tremoloSamplesUntilFlip == 0)
            {
                bowDirection = -bowDirection;
                tremoloSamplesUntilFlip = std::max<std::int64_t>(
                    1, static_cast<std::int64_t>(
                        sampleRate / tremoloReversalsPerSecond));
            }
        }

        if (shuffleSamplesUntilFlip > 0 && shuffleSubdivisionsPerSecond > 0.0)
        {
            --shuffleSamplesUntilFlip;
            if (shuffleSamplesUntilFlip == 0)
            {
                bowDirection = -bowDirection;
                shufflePhase = (shufflePhase + 1) % 6;

                // Two mirrored long-short-short cells:
                // long D, short U, short D, long U, short D, short U.
                static constexpr std::array<double, 6> durationUnits {
                    2.0, 1.0, 1.0, 2.0, 1.0, 1.0
                };
                static constexpr std::array<double, 6> energyScale {
                    1.12, 0.78, 0.86, 1.10, 0.78, 0.86
                };

                shuffleEnergyScale =
                    energyScale[static_cast<std::size_t>(shufflePhase)];
                shuffleSamplesUntilFlip = std::max<std::int64_t>(
                    1, static_cast<std::int64_t>(
                        durationUnits[static_cast<std::size_t>(shufflePhase)]
                        * sampleRate / shuffleSubdivisionsPerSecond));
            }
        }

        const auto p = pressure.next();
        const auto s = speed.next();
        const auto a = attack.next();
        const auto pos = position.next();
        const auto bal = balance.next();
        const auto vibWidth = vibratoWidth.next();
        const auto vibPace = vibratoPace.next();
        const auto gateValue = gate.next();

        const auto bowTargetSpeed =
            (0.04 + 0.61 * std::pow(s, 1.25)) * shuffleEnergyScale;
        // Bow Response is the player's ability to accelerate/reverse the bow,
        // not an amplitude-envelope attack. The earlier 0.25..3 m/s^2 range
        // made alternating fiddle strokes unrealistically sluggish.
        const auto bowAcceleration =
            bowResponseScale * 2.5 * std::pow(24.0, a);
        // A player's "same pressure" gesture does not produce the same usable
        // string-normal force everywhere along the speaking length. Close to the
        // bridge the string is mechanically stiffer and stable Helmholtz motion
        // needs more normal force. Compensate the player-facing control so
        // Bow Contact keeps its intended warm->bright behaviour without exposing
        // a separate force-vs-position parameter.
        // Keep the fingerboard-side gesture at the ordinary pressure baseline.
        // Only add the extra normal force required as the contact approaches the
        // stiffer bridge region. Reducing the fingerboard force made that end
        // slip/noise-rich rather than genuinely warm.
        const auto contactForceCompensation = 1.00 + 0.55 * pos;
        double strokeBiteGain = 1.0;
        if (strokeBiteSamplesRemaining > 0 && strokeBiteTotalSamples > 0)
        {
            const auto phase =
                static_cast<double>(strokeBiteSamplesRemaining)
                / static_cast<double>(strokeBiteTotalSamples);
            // Cosine-shaped preload release avoids a discontinuous force step
            // while still giving the first few milliseconds extra grip.
            const auto shaped = 0.5 - 0.5 * std::cos(pi * phase);
            strokeBiteGain += strokeBiteAmount * shaped;
            --strokeBiteSamplesRemaining;
        }

        const auto totalForce =
            (0.06 * std::pow(8.0, p))
            * contactForceCompensation
            * velocityScale
            * shuffleEnergyScale
            * strokeBiteGain
            * gateValue;
        const auto beta = bowBetaFingerboard + (bowBetaBridge - bowBetaFingerboard) * pos;

        const auto desiredSpeed =
            static_cast<double>(bowDirection) * bowTargetSpeed * gateValue;
        const auto maxDelta = bowAcceleration / sampleRate;
        bowSpeed += std::clamp(desiredSpeed - bowSpeed, -maxDelta, maxDelta);

        const auto vibratoRateHz = 4.0 + 3.0 * vibPace;
        vibratoPhase += 2.0 * pi * vibratoRateHz / sampleRate;
        if (vibratoPhase >= 2.0 * pi)
            vibratoPhase -= 2.0 * pi;

        const auto vibratoDepthCents = 35.0 * vibWidth;
        const auto vibratoWave = std::sin(vibratoPhase);
        double appliedVibratoCents = 0.0;

        std::array<double, stringCount> currentFrequency{};
        std::array<double, stringCount> bridgeDelay{};
        std::array<double, stringCount> nutDelay{};

        for (std::size_t i = 0; i < currentFrequency.size(); ++i)
        {
            currentFrequency[i] = std::max(20.0, speakingFrequency[i].next());

            const auto isFingeredPrimary =
                static_cast<int>(i) == primaryString
                && speakingFrequency[i].target > openFrequency[i] * 1.0005;
            if (isFingeredPrimary && vibratoDepthCents > 1.0e-6)
            {
                appliedVibratoCents = vibratoDepthCents * vibratoWave;
                currentFrequency[i] *= std::exp2(appliedVibratoCents / 1200.0);
            }

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
            const auto lossFiltered = runtimeLossGain[i]
                * ((1.0 - lossAlpha[i]) * incidentNut[i] + lossAlpha[i] * lossX1[i]);
            lossX1[i] = incidentNut[i];

            const auto filtered = runtimeAllpassA[i] * lossFiltered
                                + allpassX1[i]
                                - runtimeAllpassA[i] * allpassY1[i];
            allpassX1[i] = lossFiltered;
            allpassY1[i] = filtered;

            const auto fingered =
                speakingFrequency[i].target > openFrequency[i] * 1.0005;

            // A stopped string loses additional transverse energy into the
            // fingertip. The short extra loss after Note On represents the
            // finger settling onto the string; it is applied at the termination,
            // not as an output amplitude envelope.
            double fingerTerminationGain = 1.0;
            if (fingered)
            {
                fingerTerminationGain =
                    0.9975 * (1.0 - 0.0060 * fingerTouch[i]);
                const auto touchDecay =
                    std::exp(-1.0 / (sampleRate * 0.005));
                fingerTouch[i] *= touchDecay;
            }
            else
            {
                fingerTouch[i] = 0.0;
            }

            const auto chopTerminationGain =
                chopDampingActive ? 0.960 : 1.0;
            const auto reflectedNut =
                -filtered * fingerTerminationGain * chopTerminationGain;

            const auto incomingVelocity = incomingBridge[i] + incomingNut[i];
            double injection = 0.0;
            if (bowForce[i] > 1.0e-8 && std::abs(bowSpeed) > 1.0e-8)
            {
                const auto stringVelocity = contacts[i].solve(
                    incomingVelocity, bowSpeed, bowForce[i], stringImpedance[i], sampleRate,
                    staticGripScale, slidingGripScale, contactStateRateScale);
                injection = stringVelocity - incomingVelocity;
            }
            else
            {
                contacts[i].relax(sampleRate, contactStateRateScale);
            }

            toBridge[i].write(incomingNut[i] + injection);
            toNut[i].write(incomingBridge[i] + injection);
            fromBridge[i].write(reflectedBridge);
            fromNut[i].write(reflectedNut);

            debug.contactNormalForceN[i] = static_cast<float>(bowForce[i]);
            debug.speakingFrequencyHz[i] = static_cast<float>(currentFrequency[i]);
            debug.sticking[i] = contacts[i].sticking;
            debug.contactTemperatureC[i] =
                static_cast<float>(contacts[i].contactTemperatureC());
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
        debug.vibratoOffsetCents = static_cast<float>(appliedVibratoCents);
        debug.strokeBiteGain = static_cast<float>(strokeBiteGain);
        debug.bowDirection = bowDirection;
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
void FiddleEngine::beginBowStroke(bool alternateDirection) noexcept { impl_->beginBowStroke(alternateDirection); }
void FiddleEngine::noteOn(float frequencyHz, float velocity) { impl_->noteOn(frequencyHz, velocity); }
void FiddleEngine::setFingeringLayout(const std::array<float, 4>& frequencyHz,
                                      int primaryString,
                                      int bowPairLowerString,
                                      float velocity)
{
    impl_->setFingeringLayout(frequencyHz, primaryString, bowPairLowerString, velocity);
}
void FiddleEngine::retune(float frequencyHz) { impl_->retune(frequencyHz); }
void FiddleEngine::setStrokeBite(float amount, float durationSeconds) noexcept
{
    impl_->setStrokeBite(amount, durationSeconds);
}
void FiddleEngine::startBow(int direction) noexcept { impl_->startBow(direction); }
void FiddleEngine::startShortStroke(int direction, float durationSeconds) noexcept
{
    impl_->startShortStroke(direction, durationSeconds);
}
void FiddleEngine::startChop(int direction, float durationSeconds) noexcept
{
    impl_->startChop(direction, durationSeconds);
}
void FiddleEngine::startTremolo(float reversalsPerSecond) noexcept
{
    impl_->startTremolo(reversalsPerSecond);
}
void FiddleEngine::startShuffle(float subdivisionsPerSecond) noexcept
{
    impl_->startShuffle(subdivisionsPerSecond);
}
void FiddleEngine::stopBow() noexcept { impl_->stopBow(); }
void FiddleEngine::noteOff() { impl_->noteOff(); }
void FiddleEngine::setControls(const Controls& controls) noexcept { impl_->setControls(controls); }
void FiddleEngine::setMaterials(const MaterialSettings& materials) noexcept { impl_->setMaterials(materials); }

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
