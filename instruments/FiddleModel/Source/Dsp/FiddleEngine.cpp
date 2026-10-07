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

namespace
{
double rosinSurfaceSample(double coordinate, std::uint32_t seed) noexcept
{
    const auto lattice = static_cast<std::int64_t>(std::floor(coordinate));
    const auto frac = coordinate - static_cast<double>(lattice);
    const auto smooth = frac * frac * (3.0 - 2.0 * frac);

    const auto hash = [seed](std::int64_t index) noexcept
    {
        auto x = static_cast<std::uint64_t>(index)
            ^ (static_cast<std::uint64_t>(seed) << 32u);
        x ^= x >> 30u;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27u;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31u;
        const auto unit = static_cast<double>(x & 0xFFFFFFu)
            / static_cast<double>(0xFFFFFFu);
        return 2.0 * unit - 1.0;
    };

    const auto a = hash(lattice);
    const auto b = hash(lattice + 1);
    return a + smooth * (b - a);
}
} // namespace

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
    std::array<double, stringCount> bridgeLoadPhaseDelay{};
    std::array<double, stringCount> runtimeLossGain = lossGain;
    std::array<double, stringCount> runtimeAllpassA = allpassA;
    std::array<double, stringCount> fingerTouch{};
    std::array<double, stringCount> fingerLossX1{};
    std::array<BowContact, stringCount> contacts{};
    std::array<std::uint32_t, stringCount> rosinNoiseState {
        0x13579BDFu, 0x2468ACE1u, 0xA5A5F00Du, 0xC001D00Du
    };
    std::array<double, stringCount> rosinNoisePrevious{};
    std::array<double, stringCount> rosinSurfaceCoordinate{};
    std::array<double, stringCount> rosinNoiseEnvelope{};
    std::array<double, stringCount> rosinTransitionEnvelope{};
    double rosinNoiseScale = 1.0;

    ModalBank body{};
    ModalBank bodyRocking{true};
    RadiationFilter radiationLeft{};
    RadiationFilter radiationRight{};
    BowGeometryMapper bowGeometry{};
    double rockingRadiationLow = 0.0;
    double rockingRadiationAlpha = 0.0;

    Smoother pressure{};
    Smoother speed{};
    Smoother attack{};
    Smoother position{};
    Smoother balance{};
    Smoother singleStringIsolation{};
    Smoother vibratoWidth{};
    Smoother vibratoPace{};
    Smoother gate{};
    std::array<Smoother, stringCount> speakingFrequency{};
    std::array<std::int64_t, stringCount> fastFingeringSamplesRemaining{};

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
    std::int64_t oneShotReleaseSamplesRemaining = 0;
    std::int64_t oneShotReleaseTotalSamples = 0;
    double oneShotLiftBrake = 2.0;
    double oneShotLiftForceCurve = 1.0;
    std::int64_t chopDampingSamplesRemaining = 0;
    std::int64_t chopImpactSamplesRemaining = 0;
    std::int64_t chopImpactTotalSamples = 0;
    double chopImpactVelocityPeakMps = 0.0;
    std::int64_t tremoloSamplesUntilFlip = 0;
    double tremoloReversalsPerSecond = 0.0;
    std::int64_t shuffleSamplesUntilFlip = 0;
    double shuffleSubdivisionsPerSecond = 0.0;
    int shufflePhase = 0;
    double shuffleEnergyScale = 1.0;
    double strokeBiteAmount = 0.0;
    std::int64_t strokeBiteSamplesRemaining = 0;
    std::int64_t strokeBiteTotalSamples = 0;
    double reversalAccelerationBoost = 0.0;
    std::int64_t reversalAssistSamplesRemaining = 0;
    std::int64_t reversalAssistTotalSamples = 0;
    int primaryString = 1;
    int pairLower = 1;

    void prepare(double newSampleRate)
    {
        sampleRate = std::clamp(newSampleRate, 32000.0, 192000.0);
        body.prepare(sampleRate);
        bodyRocking.prepare(sampleRate);
        // Two nearby radiation angles: left keeps slightly more body, right
        // slightly more bridge air. The mechanical body itself remains shared.
        radiationLeft.prepare(sampleRate, 6900.0, 0.19);
        radiationRight.prepare(sampleRate, 7700.0, 0.25);
        // Rocking motion behaves increasingly like a directional radiator as
        // wavelength shortens. Keep low-frequency rocking close to the mono
        // body core and allow only a little more stereo radiation above the
        // body-presence region. This acts after the shared mechanics and never
        // duplicates/detunes the physical strings or body.
        constexpr double rockingRadiationCrossoverHz = 1200.0;
        rockingRadiationAlpha =
            1.0 - std::exp(
                -2.0 * pi * rockingRadiationCrossoverHz / sampleRate);
        bowGeometry.prepare();
        materialConfigured = false;
        setMaterials(materialSettings);

        pressure.prepare(sampleRate, 0.012);
        speed.prepare(sampleRate, 0.012);
        attack.prepare(sampleRate, 0.020);
        position.prepare(sampleRate, 0.015);
        balance.prepare(sampleRate, 0.015);
        singleStringIsolation.prepare(sampleRate, 0.010);
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
        fingerLossX1.fill(0.0);
        allpassY1.fill(0.0);
        rosinNoiseState = {
            0x13579BDFu, 0x2468ACE1u, 0xA5A5F00Du, 0xC001D00Du
        };
        rosinNoisePrevious.fill(0.0);
        rosinSurfaceCoordinate = { 17.25, 53.75, 91.50, 137.0 };
        rosinNoiseEnvelope.fill(0.0);
        rosinTransitionEnvelope.fill(0.0);
        body.reset();
        bodyRocking.reset();
        radiationLeft.reset();
        radiationRight.reset();
        rockingRadiationLow = 0.0;

        pressure.reset(controlTargets.pressure);
        speed.reset(controlTargets.speed);
        attack.reset(controlTargets.attack);
        position.reset(controlTargets.position);
        balance.reset(controlTargets.balance);
        singleStringIsolation.reset(controlTargets.singleStringIsolation);
        vibratoWidth.reset(controlTargets.vibratoWidth);
        vibratoPace.reset(controlTargets.vibratoPace);
        gate.reset(0.0);

        for (std::size_t i = 0; i < speakingFrequency.size(); ++i)
            speakingFrequency[i].reset(openFrequency[i]);
        fastFingeringSamplesRemaining.fill(0);

        velocityScale = 1.0;
        bowSpeed = 0.0;
        vibratoPhase = 0.0;
        bowDirection = 1;
        bowStrokeStarted = false;
        shortStrokeSamplesRemaining = 0;
        oneShotReleaseSamplesRemaining = 0;
        oneShotReleaseTotalSamples = 0;
        oneShotLiftBrake = 2.0;
        oneShotLiftForceCurve = 1.0;
        chopDampingSamplesRemaining = 0;
        chopImpactSamplesRemaining = 0;
        chopImpactTotalSamples = 0;
        chopImpactVelocityPeakMps = 0.0;
        tremoloSamplesUntilFlip = 0;
        tremoloReversalsPerSecond = 0.0;
        shuffleSamplesUntilFlip = 0;
        shuffleSubdivisionsPerSecond = 0.0;
        shufflePhase = 0;
        shuffleEnergyScale = 1.0;
        strokeBiteAmount = 0.0;
        strokeBiteSamplesRemaining = 0;
        strokeBiteTotalSamples = 0;
        reversalAccelerationBoost = 0.0;
        reversalAssistSamplesRemaining = 0;
        reversalAssistTotalSamples = 0;
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
        controlTargets.singleStringIsolation =
            static_cast<float>(clamp01(controls.singleStringIsolation));
        controlTargets.vibratoWidth = static_cast<float>(clamp01(controls.vibratoWidth));
        controlTargets.vibratoPace = static_cast<float>(clamp01(controls.vibratoPace));

        pressure.setTarget(controlTargets.pressure);
        speed.setTarget(controlTargets.speed);
        attack.setTarget(controlTargets.attack);
        position.setTarget(controlTargets.position);
        balance.setTarget(controlTargets.balance);
        singleStringIsolation.setTarget(controlTargets.singleStringIsolation);
        vibratoWidth.setTarget(controlTargets.vibratoWidth);
        vibratoPace.setTarget(controlTargets.vibratoPace);
    }

    [[nodiscard]] double bridgeReflectionPhaseDelaySamples(
        std::size_t stringIndex,
        double frequencyHz) const noexcept
    {
        const auto omega =
            2.0 * pi * std::max(20.0, frequencyHz) / sampleRate;
        const auto translation = body.responseAt(frequencyHz);
        const auto rocking = bodyRocking.responseAt(frequencyHz);

        constexpr double halfBridgeSpan = geSpacingMm * 0.5;
        std::array<double, stringCount> lever {};
        double impedanceSum = 0.0;
        double impedanceLeverSum = 0.0;
        double impedanceLeverSquaredSum = 0.0;

        for (std::size_t i = 0; i < stringCount; ++i)
        {
            lever[i] = bridgeXmm[i] / halfBridgeSpan;
            impedanceSum += stringImpedance[i];
            impedanceLeverSum += stringImpedance[i] * lever[i];
            impedanceLeverSquaredSum +=
                stringImpedance[i] * lever[i] * lever[i];
        }

        const auto a00 = 1.0 + translation * impedanceSum;
        const auto a01 = translation * impedanceLeverSum;
        const auto a10 = rocking * impedanceLeverSum;
        const auto a11 =
            1.0 + rocking * impedanceLeverSquaredSum;
        const auto incidentForce =
            2.0 * stringImpedance[stringIndex];
        const auto incidentRocking =
            lever[stringIndex] * incidentForce;
        const auto rhs0 = translation * incidentForce;
        const auto rhs1 = rocking * incidentRocking;
        const auto determinant = a00 * a11 - a01 * a10;

        if (std::abs(determinant) < 1.0e-12)
            return 0.0;

        const auto bridgeVelocity =
            (rhs0 * a11 - a01 * rhs1) / determinant;
        const auto rockingVelocity =
            (a00 * rhs1 - a10 * rhs0) / determinant;
        const auto reflection =
            bridgeVelocity
            + lever[stringIndex] * rockingVelocity
            - 1.0;

        auto phaseOffset = std::arg(reflection) - pi;
        while (phaseOffset <= -pi)
            phaseOffset += 2.0 * pi;
        while (phaseOffset > pi)
            phaseOffset -= 2.0 * pi;

        return std::clamp(-phaseOffset / omega, -0.25, 0.25);
    }

    void refreshBridgeLoadPhaseDelay(
        std::size_t stringIndex,
        double frequencyHz) noexcept
    {
        bridgeLoadPhaseDelay[stringIndex] =
            bridgeReflectionPhaseDelaySamples(stringIndex, frequencyHz);
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
                bodyRocking.setMaterialScales(1.00, 1.00, 0.17);
                break;
            case BodyMaterialPreset::LightStiffComposite:
                body.setMaterialScales(1.04, 0.82, 1.05);
                bodyRocking.setMaterialScales(1.03, 0.90, 0.18);
                break;
            case BodyMaterialPreset::DenseExperimental:
                body.setMaterialScales(0.97, 1.28, 0.90);
                bodyRocking.setMaterialScales(0.98, 1.32, 0.15);
                break;
            case BodyMaterialPreset::RigidComposite:
                body.setMaterialScales(1.08, 0.68, 0.96);
                bodyRocking.setMaterialScales(1.06, 0.76, 0.16);
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
                rosinNoiseScale = 1.00;
                break;
            case ContactMaterialPreset::DryLightGrip:
                staticGripScale = 0.86;
                slidingGripScale = 0.90;
                contactStateRateScale = 1.16;
                // Dry/light grip should retain a clearly granular, airy
                // microscopic contact texture after the new smoothed
                // stick/slip roughness model.
                rosinNoiseScale = 2.00;
                break;
            case ContactMaterialPreset::HighGripRosin:
                staticGripScale = 1.18;
                slidingGripScale = 1.08;
                contactStateRateScale = 0.84;
                rosinNoiseScale = 0.72;
                break;
            case ContactMaterialPreset::SyntheticHair:
                staticGripScale = 0.93;
                slidingGripScale = 0.95;
                contactStateRateScale = 1.05;
                rosinNoiseScale = 0.88;
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

            const auto currentTarget =
                speakingFrequency[i].target > 20.0
                    ? speakingFrequency[i].target
                    : openFrequency[i];
            refreshBridgeLoadPhaseDelay(i, currentTarget);
        }
    }

    void beginBowStroke(bool alternateDirection) noexcept
    {
        if (!bowStrokeStarted)
        {
            bowStrokeStarted = true;
            bowDirection = 1;

            // Starting from a stationary bow needs a small physical preload:
            // hair is already resting lightly on the string before the player
            // drives the first stroke. This avoids making the first fiddle note
            // much weaker than subsequent bow changes while keeping the full
            // pressure/speed ramp in the contact model.
            gate.reset(std::max(gate.current, 0.12));
            retriggerBowCatch(0.10, 0.006);
            triggerBowReversalAssist(1.8, 0.008);
            return;
        }

        if (alternateDirection)
        {
            bowDirection = -bowDirection;
            // Real bow changes are driven by a short wrist/forearm acceleration
            // pulse. Keep that gesture in the mechanical bow state instead of
            // hiding the reversal with an output transient.
            triggerBowReversalAssist(2.4, 0.008);
        }
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

            const auto previousTarget = speakingFrequency[i].target;
            speakingFrequency[i].setTarget(target);

            // A normal violin fingering change is a fast relocation of the
            // stopped termination, not a synthesizer-style portamento. Keep
            // initial note capture on the calibrated 18 ms path, but while a
            // bow is already moving let the fingertip establish the new
            // speaking length within a few milliseconds. A true finger slide
            // should be modelled as a separate gesture, not inferred from
            // ordinary MIDI-note overlap.
            if (gate.target > 0.5
                && std::abs(target - previousTarget) > 0.25)
            {
                speakingFrequency[i].prepare(sampleRate, 0.0015);
                fastFingeringSamplesRemaining[i] =
                    std::max<std::int64_t>(
                        1,
                        static_cast<std::int64_t>(0.012 * sampleRate));
            }

            refreshBridgeLoadPhaseDelay(i, target);
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
        refreshBridgeLoadPhaseDelay(primary, requested);
    }

    void primeUncontactedBowGesture(double seconds) noexcept
    {
        // A held MIDI bow action prepares its force/travel/response controls
        // while no string is selected. Standalone's mouse audition instead
        // sends bow and fingering together, otherwise meeting the string
        // before those physical controls have settled. Advance *only* their
        // uncontacted controller states through the same pre-bow preparation
        // interval. Do not advance the waveguide, rosin surface or time-domain
        // audio; the musician has only prepared the bow before contact.
        if (gate.target > 0.5)
            return;

        const auto steps = static_cast<int>(
            std::clamp(seconds, 0.0, 0.040) * sampleRate);
        for (int step = 0; step < steps; ++step)
        {
            pressure.next();
            speed.next();
            attack.next();
            position.next();
            balance.next();
            singleStringIsolation.next();
            vibratoWidth.next();
            vibratoPace.next();
        }
    }

    void setStrokeBite(double amount, double durationSeconds) noexcept
    {
        strokeBiteAmount = std::clamp(amount, 0.0, 0.80);
        strokeBiteTotalSamples = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                std::clamp(durationSeconds, 0.001, 0.030) * sampleRate));
        strokeBiteSamplesRemaining = strokeBiteTotalSamples;
    }

    void retriggerBowCatch(double amount, double durationSeconds) noexcept
    {
        // Used by internally scheduled bow reversals (tremolo/shuffle). This is
        // a brief extra normal-force preload at the physical contact, not an
        // output-envelope transient.
        setStrokeBite(amount, durationSeconds);
    }

    void triggerBowReversalAssist(double amount, double durationSeconds) noexcept
    {
        reversalAccelerationBoost = std::clamp(amount, 0.0, 5.0);
        reversalAssistTotalSamples = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                std::clamp(durationSeconds, 0.001, 0.020) * sampleRate));
        reversalAssistSamplesRemaining = reversalAssistTotalSamples;
    }

    void startBow(int direction) noexcept
    {
        const auto newDirection = direction < 0 ? -1 : 1;
        const bool reversingMovingBow =
            bowSpeed * static_cast<double>(newDirection) < -0.02;

        bowStrokeStarted = true;
        bowDirection = newDirection;
        if (reversingMovingBow)
            triggerBowReversalAssist(2.6, 0.008);
        shortStrokeSamplesRemaining = 0;
        oneShotReleaseSamplesRemaining = 0;
        oneShotReleaseTotalSamples = 0;
        chopDampingSamplesRemaining = 0;
        chopImpactSamplesRemaining = 0;
        chopImpactTotalSamples = 0;
        chopImpactVelocityPeakMps = 0.0;
        tremoloSamplesUntilFlip = 0;
        tremoloReversalsPerSecond = 0.0;
        shuffleSamplesUntilFlip = 0;
        shuffleSubdivisionsPerSecond = 0.0;
        shufflePhase = 0;
        shuffleEnergyScale = 1.0;
        gate.setTarget(1.0);
    }

    void startShortStroke(int direction,
                          double durationSeconds,
                          double liftDurationSeconds,
                          double liftBrake,
                          double liftForceCurve) noexcept
    {
        startBow(direction);
        shortStrokeSamplesRemaining = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(durationSeconds * sampleRate));
        oneShotReleaseTotalSamples = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                std::clamp(liftDurationSeconds, 0.002, 0.030) * sampleRate));
        oneShotReleaseSamplesRemaining = 0;
        oneShotLiftBrake = std::clamp(liftBrake, 0.0, 8.0);
        oneShotLiftForceCurve = std::clamp(liftForceCurve, 0.35, 3.0);
    }

    void startChop(int direction,
                   double durationSeconds,
                   double impactVelocityMps,
                   double impactDurationSeconds) noexcept
    {
        startShortStroke(direction, durationSeconds, 0.0045, 6.0, 2.4);
        chopDampingSamplesRemaining = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                std::max(0.020, durationSeconds + 0.012) * sampleRate));

        chopImpactVelocityPeakMps =
            std::clamp(impactVelocityMps, 0.0, 0.040);
        chopImpactTotalSamples = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                std::clamp(impactDurationSeconds, 0.0008, 0.0050)
                * sampleRate));
        chopImpactSamplesRemaining = chopImpactTotalSamples;
    }

    void startTremolo(double reversalsPerSecond) noexcept
    {
        bowStrokeStarted = true;
        if (bowDirection == 0)
            bowDirection = 1;

        shortStrokeSamplesRemaining = 0;
        oneShotReleaseSamplesRemaining = 0;
        oneShotReleaseTotalSamples = 0;
        chopDampingSamplesRemaining = 0;
        chopImpactSamplesRemaining = 0;
        chopImpactTotalSamples = 0;
        chopImpactVelocityPeakMps = 0.0;
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
        oneShotReleaseSamplesRemaining = 0;
        oneShotReleaseTotalSamples = 0;
        chopDampingSamplesRemaining = 0;
        chopImpactSamplesRemaining = 0;
        chopImpactTotalSamples = 0;
        chopImpactVelocityPeakMps = 0.0;
        tremoloSamplesUntilFlip = 0;
        tremoloReversalsPerSecond = 0.0;

        shuffleSubdivisionsPerSecond =
            std::clamp(subdivisionsPerSecond, 6.0, 20.0);
        shufflePhase = 0;
        shuffleEnergyScale = 1.22;

        // First stroke is the long member of a long-short-short bowing cell.
        shuffleSamplesUntilFlip = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                2.0 * sampleRate / shuffleSubdivisionsPerSecond));
        gate.setTarget(1.0);
    }

    void stopBow() noexcept
    {
        shortStrokeSamplesRemaining = 0;
        oneShotReleaseSamplesRemaining = 0;
        oneShotReleaseTotalSamples = 0;
        chopDampingSamplesRemaining = 0;
        chopImpactSamplesRemaining = 0;
        chopImpactTotalSamples = 0;
        chopImpactVelocityPeakMps = 0.0;
        tremoloSamplesUntilFlip = 0;
        tremoloReversalsPerSecond = 0.0;
        shuffleSamplesUntilFlip = 0;
        shuffleSubdivisionsPerSecond = 0.0;
        shufflePhase = 0;
        shuffleEnergyScale = 1.0;
        reversalAccelerationBoost = 0.0;
        reversalAssistSamplesRemaining = 0;
        reversalAssistTotalSamples = 0;
        gate.setTarget(0.0);
    }

    void noteOff() noexcept
    {
        stopBow();
    }

    std::array<double, stringCount> makeBowForces(
        double totalForce,
        double balanceValue,
        double isolationAmount) noexcept
    {
        const auto geometry = bowGeometry.solve(
            pairLower, totalForce, balanceValue);
        debug.bowAngleDeg = static_cast<float>(geometry.bowAngleDeg);

        auto forces = geometry.normalForceN;
        isolationAmount = clamp01(isolationAmount);
        if (isolationAmount <= 1.0e-6 || std::abs(balanceValue) < 0.80)
            return forces;

        // A deliberate single-string lean narrows the effective hair footprint.
        // The neighbouring string is still present in the waveguide and shared
        // bridge/body, so sympathetic resonance remains; only direct bow force
        // on that neighbour is reduced. Reassign the unloaded force to the
        // primary string to conserve the player's total normal force.
        const auto lower = static_cast<std::size_t>(pairLower);
        const auto upper = lower + 1;
        const auto primary = static_cast<std::size_t>(
            std::clamp(primaryString, pairLower, pairLower + 1));
        const auto neighbour = primary == lower ? upper : lower;
        // A player leaning the hair onto one string should not keep
        // directly driving the neighbouring open string. Leave only a tiny
        // residual footprint for hair width; sympathetic motion still travels
        // through the shared bridge/body waveguide. The earlier 15% residual
        // could become perceptually dominant on low strings once the body
        // fundamentals were restored.
        // The G string is the outside string of the bridge. During an
        // intentional monophonic G stroke the hair cannot also directly bow
        // open D: even the old 3% residue may sustain a second audible
        // stationary pitch. Preserve D's sympathetic motion through the
        // bridge/rocking junction. Keep the small residual compliance for
        // the other string pairs, where the same-string identity regression
        // has shown that removing it globally is not acceptable.
        const auto residualContact =
            primaryString == 0 ? 0.0 : 0.03;
        const auto neighbourScale =
            1.0 - (1.0 - residualContact) * isolationAmount;
        const auto removed =
            forces[neighbour] * (1.0 - neighbourScale);
        forces[neighbour] *= neighbourScale;
        forces[primary] += removed;
        return forces;
    }

    std::array<double, 2> processSample() noexcept
    {
        if (shortStrokeSamplesRemaining > 0)
        {
            --shortStrokeSamplesRemaining;
            if (shortStrokeSamplesRemaining == 0)
                oneShotReleaseSamplesRemaining =
                    oneShotReleaseTotalSamples;
        }

        double oneShotLiftGain = 1.0;
        double oneShotReleaseAccelerationGain = 1.0;
        if (oneShotReleaseSamplesRemaining > 0
            && oneShotReleaseTotalSamples > 0)
        {
            const auto phase = std::clamp(
                static_cast<double>(oneShotReleaseSamplesRemaining)
                    / static_cast<double>(oneShotReleaseTotalSamples),
                0.0, 1.0);

            // Mechanical bow lift only: normal force and commanded bow travel
            // disappear while the string/body waveguide is left free to ring.
            oneShotLiftGain =
                std::pow(phase, oneShotLiftForceCurve);
            oneShotReleaseAccelerationGain =
                1.0 + oneShotLiftBrake
                    * (1.0 - 0.45 * oneShotLiftGain);

            --oneShotReleaseSamplesRemaining;
            if (oneShotReleaseSamplesRemaining == 0)
            {
                oneShotLiftGain = 0.0;
                gate.reset(0.0);
            }
        }

        const bool chopDampingActive = chopDampingSamplesRemaining > 0;
        if (chopDampingSamplesRemaining > 0)
            --chopDampingSamplesRemaining;

        double chopImpactVelocity = 0.0;
        if (chopImpactSamplesRemaining > 0
            && chopImpactTotalSamples > 0)
        {
            const auto progress = 1.0
                - static_cast<double>(chopImpactSamplesRemaining)
                    / static_cast<double>(chopImpactTotalSamples);
            // Half-sine transverse collision at the bowing point. This is an
            // internal waveguide velocity impulse, not an output click/envelope.
            chopImpactVelocity =
                static_cast<double>(bowDirection)
                * chopImpactVelocityPeakMps
                * std::sin(pi * std::clamp(progress, 0.0, 1.0));
            --chopImpactSamplesRemaining;
        }

        if (tremoloSamplesUntilFlip > 0 && tremoloReversalsPerSecond > 0.0)
        {
            --tremoloSamplesUntilFlip;
            if (tremoloSamplesUntilFlip == 0)
            {
                bowDirection = -bowDirection;
                retriggerBowCatch(0.10, 0.0045);
                triggerBowReversalAssist(3.2, 0.0080);
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
                    1.22, 0.68, 0.82, 1.18, 0.68, 0.82
                };

                shuffleEnergyScale =
                    energyScale[static_cast<std::size_t>(shufflePhase)];
                retriggerBowCatch(
                    0.10 + 0.10 * shuffleEnergyScale, 0.0055);
                triggerBowReversalAssist(3.0, 0.0090);
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
        const auto singleIsolation = singleStringIsolation.next();

        const auto vibWidth = vibratoWidth.next();
        const auto vibPace = vibratoPace.next();
        const auto gateValue = gate.next();

        const auto bowTargetSpeed =
            (0.04 + 0.61 * std::pow(s, 1.25)) * shuffleEnergyScale;
        // Bow Response is the player's ability to accelerate/reverse the bow,
        // not an amplitude-envelope attack. The earlier 0.25..3 m/s^2 range
        // made alternating fiddle strokes unrealistically sluggish.
        const auto baseBowAcceleration =
            bowResponseScale * 2.5 * std::pow(24.0, a);
        double reversalAccelerationGain = 1.0;
        if (reversalAssistSamplesRemaining > 0
            && reversalAssistTotalSamples > 0)
        {
            const auto phase =
                static_cast<double>(reversalAssistSamplesRemaining)
                / static_cast<double>(reversalAssistTotalSamples);
            // Smoothly release the acceleration pulse. This changes bow
            // kinematics only; it is not an audio amplitude envelope.
            const auto shaped = phase * phase * (3.0 - 2.0 * phase);
            reversalAccelerationGain +=
                reversalAccelerationBoost * shaped;
            --reversalAssistSamplesRemaining;
        }
        const auto bowAcceleration =
            baseBowAcceleration
            * reversalAccelerationGain
            * oneShotReleaseAccelerationGain;
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
            * gateValue
            * oneShotLiftGain;
        const auto beta = bowBetaFingerboard + (bowBetaBridge - bowBetaFingerboard) * pos;

        const auto desiredSpeed =
            static_cast<double>(bowDirection)
            * bowTargetSpeed
            * gateValue
            * oneShotLiftGain;
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
        std::array<double, stringCount> finiteWidthHalfDelay{};

        for (std::size_t i = 0; i < currentFrequency.size(); ++i)
        {
            currentFrequency[i] = std::max(20.0, speakingFrequency[i].next());

            if (fastFingeringSamplesRemaining[i] > 0)
            {
                --fastFingeringSamplesRemaining[i];
                if (fastFingeringSamplesRemaining[i] == 0)
                    speakingFrequency[i].prepare(sampleRate, 0.018);
            }

            const auto isFingeredPrimary =
                static_cast<int>(i) == primaryString
                && speakingFrequency[i].target > openFrequency[i] * 1.0005;
            if (isFingeredPrimary && vibratoDepthCents > 1.0e-6)
            {
                appliedVibratoCents = vibratoDepthCents * vibratoWave;
                currentFrequency[i] *= std::exp2(appliedVibratoCents / 1200.0);
            }

            const auto fingeredForReflection =
                speakingFrequency[i].target > openFrequency[i] * 1.0005;
            const auto fingerPhaseDelay = fingeredForReflection
                ? firReflectionPhaseDelaySamples(
                    sampleRate,
                    currentFrequency[i],
                    fingerReflectionAlpha[i])
                : 0.0;
            auto oneWay =
                sampleRate / (2.0 * currentFrequency[i])
                - 0.5 * (
                    filterPhaseDelay[i]
                    + bridgeLoadPhaseDelay[i]
                    + fingerPhaseDelay);
            oneWay = std::clamp(oneWay, 4.0, static_cast<double>(delaySize - 8));
            bridgeDelay[i] = std::max(1.2, oneWay * beta);
            nutDelay[i] = std::max(1.2, oneWay * (1.0 - beta));

            // Convert half the physical hair-ribbon width into propagation
            // delay along this speaking length. For the fundamental this is a
            // small phase span; upper partials see progressively more spatial
            // variation across the contact patch.
            finiteWidthHalfDelay[i] = std::clamp(
                oneWay
                    * (0.5 * bowHairContactWidthMeters
                       / violinSpeakingLengthMeters),
                0.0,
                3.0);
        }

        std::array<double, stringCount> incidentBridge{};
        std::array<double, stringCount> incidentNut{};
        std::array<double, stringCount> incomingBridge{};
        std::array<double, stringCount> incomingNut{};

        std::array<double, stringCount> bridgeLever {};
        double incidentForce = 0.0;
        double incidentRocking = 0.0;
        double impedanceSum = 0.0;
        double impedanceLeverSum = 0.0;
        double impedanceLeverSquaredSum = 0.0;
        constexpr double halfBridgeSpan = geSpacingMm * 0.5;

        for (std::size_t i = 0; i < stringCount; ++i)
        {
            incidentBridge[i] = toBridge[i].read(bridgeDelay[i]);
            incidentNut[i] = toNut[i].read(nutDelay[i]);
            incomingBridge[i] = fromBridge[i].read(bridgeDelay[i]);
            incomingNut[i] = fromNut[i].read(nutDelay[i]);
            debug.incidentBridgeVelocityMps[i] =
                static_cast<float>(incidentBridge[i]);

            bridgeLever[i] = bridgeXmm[i] / halfBridgeSpan;
            const auto incident =
                2.0 * stringImpedance[i] * incidentBridge[i];

            incidentForce += incident;
            incidentRocking += bridgeLever[i] * incident;
            impedanceSum += stringImpedance[i];
            impedanceLeverSum +=
                stringImpedance[i] * bridgeLever[i];
            impedanceLeverSquaredSum +=
                stringImpedance[i] * bridgeLever[i] * bridgeLever[i];
        }

        // Two coupled generalized bridge coordinates:
        // vertical translation and left/right rocking. Each string sees
        // v_i = translation + lever_i * rocking, so outer strings couple
        // more strongly to the rocking coordinate than inner strings.
        const auto directTranslation = body.direct();
        const auto directRocking = bodyRocking.direct();
        const auto a00 = 1.0 + directTranslation * impedanceSum;
        const auto a01 = directTranslation * impedanceLeverSum;
        const auto a10 = directRocking * impedanceLeverSum;
        const auto a11 =
            1.0 + directRocking * impedanceLeverSquaredSum;
        const auto rhs0 =
            body.knownPart() + directTranslation * incidentForce;
        const auto rhs1 =
            bodyRocking.knownPart()
            + directRocking * incidentRocking;
        const auto determinant =
            std::max(1.0e-12, a00 * a11 - a01 * a10);

        const auto bridgeVelocity =
            (rhs0 * a11 - a01 * rhs1) / determinant;
        const auto bridgeRockingVelocity =
            (a00 * rhs1 - a10 * rhs0) / determinant;

        const auto bodyForce =
            incidentForce
            - impedanceSum * bridgeVelocity
            - impedanceLeverSum * bridgeRockingVelocity;
        const auto bodyRockingForce =
            incidentRocking
            - impedanceLeverSum * bridgeVelocity
            - impedanceLeverSquaredSum * bridgeRockingVelocity;

        body.push(bodyForce);
        bodyRocking.push(bodyRockingForce);

        // Mechanical bridge mobility and acoustic radiation are related but
        // not identical. The solve above intentionally includes a broadband
        // conductance so the strings see a realistic nonzero bridge mobility
        // between resonances. For radiation, distributed resonant body motion
        // should dominate; otherwise the broadband bridge velocity passes an
        // almost ideal string waveform straight to the listener.
        const auto bodyModalVelocity = body.currentModalVelocity();
        const auto rockingModalVelocity = bodyRocking.currentModalVelocity();

        std::array<double, stringCount> bridgeStringVelocity {};
        for (std::size_t i = 0; i < stringCount; ++i)
            bridgeStringVelocity[i] =
                bridgeVelocity
                + bridgeLever[i] * bridgeRockingVelocity;

        const auto bowForce = makeBowForces(
            totalForce, bal, singleIsolation);

        std::array<double, stringCount> chopImpactInjection {};
        if (std::abs(chopImpactVelocity) > 1.0e-12)
        {
            // Reuse the bridge-curvature/String Focus geometry to distribute
            // the collision over the active pair. Normalize it so the impact
            // does not depend on the slower pressure/gate smoothers.
            const auto impactForce =
                bowGeometry.solve(pairLower, 0.25, bal).normalForceN;
            double impactForceSum = 0.0;
            for (const auto force : impactForce)
                impactForceSum += force;

            if (impactForceSum > 1.0e-12)
            {
                for (std::size_t i = 0; i < stringCount; ++i)
                    chopImpactInjection[i] =
                        chopImpactVelocity * impactForce[i] / impactForceSum;
            }
        }

        debug.contactNormalForceN.fill(0.0f);
        debug.sticking.fill(false);
        debug.rosinNoiseVelocityMps.fill(0.0f);
        debug.rosinTransitionEnvelope.fill(0.0f);
        debug.finiteWidthContactVelocityDeltaMps.fill(0.0f);

        for (std::size_t i = 0; i < stringCount; ++i)
        {
            const auto reflectedBridge =
                bridgeStringVelocity[i] - incidentBridge[i];
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
            auto fingerFiltered = filtered;
            if (fingered)
            {
                const auto fingerAlpha = fingerReflectionAlpha[i];
                fingerFiltered =
                    (1.0 - fingerAlpha) * filtered
                    + fingerAlpha * fingerLossX1[i];
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
            // Track the previous termination sample even while open so a new
            // stopped note starts from a continuous physical reflection state.
            fingerLossX1[i] = filtered;

            const auto chopTerminationGain =
                chopDampingActive ? 0.960 : 1.0;

            // The rigid two-coordinate bridge is intentionally economical, but
            // it otherwise gives unused open strings an unrealistically high-Q
            // cross-string drive. In monophonic single-string play, a real
            // instrument still has sympathetic resonance, but it must not grow
            // until the adjacent open string becomes the perceptual main note.
            // Add a small passive loss only to non-primary strings while the
            // bow footprint is deliberately isolated. Drone/double-stop playing
            // keeps full resonance because singleStringIsolation is then zero.
            const auto isUpperAdjacentSympathetic =
                static_cast<int>(i) == primaryString + 1;
            // Keep the established gentle 3.5% passive loss on an
            // unplayed adjacent string. The apparent louder open D in the
            // GUI G3 regression came from *shared upper G partials* (G9/D6
            // and G12/D8) being wrongly counted as separate D energy.
            // Fix the detector instead of artificially muting sympathetic
            // physical coupling to chase a misleading score.
            const auto sympatheticLoss =
                isUpperAdjacentSympathetic
                    ? 1.0 - 0.035 * singleIsolation
                    : 1.0;
            const auto reflectedNut =
                -fingerFiltered
                * fingerTerminationGain
                * chopTerminationGain
                * sympatheticLoss;

            const auto incomingVelocity = incomingBridge[i] + incomingNut[i];

            // Approximate the finite width of the bow-hair ribbon without
            // inventing another oscillator or filtering the radiated output.
            // Read the already-existing travelling waves at virtual points
            // half a hair-width toward the bridge and nut, then let the
            // nonlinear friction law react to a conservative spatial average.
            // The force is still injected at the single waveguide junction;
            // a later multi-junction model can replace this reduction once
            // the 44.1/48 kHz regression envelope is understood.
            // Keep both virtual edges symmetric about the centre. Near
            // the bridge or nut, the 48 kHz grid cannot resolve the full
            // physical ribbon width without placing one edge inside the
            // sub-sample termination region. Shrink the *effective* width
            // there instead of clamping only one edge, which would introduce
            // an artificial phase bias and measurable pitch shift.
            const auto availableHalfWidth = std::max(
                0.0,
                std::min(
                    bridgeDelay[i] - 1.2,
                    nutDelay[i] - 1.2));
            const auto halfWidthDelay = std::min(
                finiteWidthHalfDelay[i], availableHalfWidth);
            const auto clampDelay = [](double value) noexcept
            {
                return std::clamp(
                    value, 1.2, static_cast<double>(delaySize - 8));
            };
            const auto bridgewardIncoming =
                fromBridge[i].read(clampDelay(bridgeDelay[i] - halfWidthDelay))
                + fromNut[i].read(clampDelay(nutDelay[i] + halfWidthDelay));
            const auto nutwardIncoming =
                fromBridge[i].read(clampDelay(bridgeDelay[i] + halfWidthDelay))
                + fromNut[i].read(clampDelay(nutDelay[i] - halfWidthDelay));
            const auto finiteWidthAverage =
                0.50 * incomingVelocity
                + 0.25 * (bridgewardIncoming + nutwardIncoming);
            // The single-junction reduction is reliable while the hair
            // ribbon is well resolved on both string segments. Very close to
            // the bridge, 44.1/48 kHz provides too few delay samples across
            // the short segment to represent a distributed contact faithfully.
            // Taper only that under-resolved region; normal/fingerboard bow
            // positions retain the full physical-width effect.
            const auto nearBridgeUnderResolution = std::clamp(
                (pos - 0.70) / 0.25, 0.0, 1.0);
            const auto resolvedFiniteWidthBlend =
                finiteWidthContactBlend
                * finiteWidthStringScale[i]
                * (1.0 - 0.85 * nearBridgeUnderResolution);
            const auto contactIncomingVelocity =
                incomingVelocity
                + resolvedFiniteWidthBlend
                    * (finiteWidthAverage - incomingVelocity);
            debug.finiteWidthContactVelocityDeltaMps[i] =
                static_cast<float>(
                    contactIncomingVelocity - incomingVelocity);

            double injection = 0.0;
            double rosinNoiseVelocity = 0.0;
            if (bowForce[i] > 1.0e-8 && std::abs(bowSpeed) > 1.0e-8)
            {
                // Deterministic microscopic hair/rosin roughness. One physical
                // roughness sample drives both a tiny friction-coefficient
                // variation and the residual contact-velocity texture.
                // Hair/rosin roughness is a spatial field attached to the
                // travelling bow hair. Faster bow motion traverses the same
                // microscopic profile faster; reversing the bow retraces it in
                // the opposite direction instead of generating unrelated noise.
                constexpr double roughnessFeaturesPerMeter = 13000.0;
                rosinSurfaceCoordinate[i] +=
                    bowSpeed * roughnessFeaturesPerMeter / sampleRate;
                const auto rawNoise = rosinSurfaceSample(
                    rosinSurfaceCoordinate[i], rosinNoiseState[i]);
                const auto differentiated =
                    rawNoise - rosinNoisePrevious[i];
                rosinNoisePrevious[i] = rawNoise;

                // Bow Contact remains the dominant spectral tilt. Bow speed now
                // changes temporal roughness mostly through spatial traversal,
                // with only a small residual colour term.
                const auto speedColour = std::clamp(
                    std::abs(bowSpeed) / 0.65, 0.0, 1.0);
                const auto brightness = std::clamp(
                    0.16 + 0.60 * pos + 0.10 * speedColour,
                    0.0, 0.92);
                const auto colouredNoise =
                    (1.0 - brightness) * 0.58 * rawNoise
                    + brightness * 0.34 * differentiated;

                const auto roughnessDepth =
                    0.008 * rosinNoiseScale * (0.85 + 0.30 * pos);
                const auto gripPerturbation = std::clamp(
                    roughnessDepth * colouredNoise, -0.06, 0.06);
                const auto localStaticGrip =
                    staticGripScale * (1.0 + 0.35 * gripPerturbation);
                const auto localSlidingGrip =
                    slidingGripScale * (1.0 + gripPerturbation);

                const auto wasSticking = contacts[i].sticking;
                const auto stringVelocity = contacts[i].solve(
                    contactIncomingVelocity,
                    bowSpeed,
                    bowForce[i],
                    stringImpedance[i],
                    sampleRate,
                    localStaticGrip,
                    localSlidingGrip,
                    contactStateRateScale);
                // Convert the nonlinear contact solution back to its
                // equivalent force-wave injection at the centre junction.
                injection = stringVelocity - contactIncomingVelocity;

                const auto transitioned =
                    wasSticking != contacts[i].sticking;
                if (transitioned)
                {
                    // Hair/rosin texture is most audible during the brief
                    // catch/release event itself. Slip onset is stronger than
                    // the return to sticking, but both decay within a few ms.
                    const auto transitionStrength =
                        contacts[i].sticking ? 0.18 : 1.0;
                    rosinTransitionEnvelope[i] =
                        std::max(
                            rosinTransitionEnvelope[i],
                            transitionStrength);
                }

                const auto slipSpeed = std::abs(contacts[i].slipSpeedMps());
                const auto gripUtilization = contacts[i].gripUtilization();
                const auto nearYield = std::clamp(
                    (gripUtilization - 0.68) / 0.32, 0.0, 1.0);
                const auto slipActivity =
                    std::clamp(slipSpeed / 0.18, 0.0, 1.0);
                const auto contactActivity = contacts[i].sticking
                    ? 0.12 * nearYield * nearYield
                    : (0.28 + 0.72 * slipActivity)
                        * std::clamp(gripUtilization / 1.2, 0.55, 1.20);

                // Microscopic surface noise should not simply become
                // quieter with less normal force. A lightly loaded contact can
                // be less stable and audibly rougher because the available
                // friction reserve is small. Keep a sub-linear force term, then
                // explicitly expose that under-gripped sliding instability.
                const auto forceScale = std::clamp(
                    std::pow(std::max(bowForce[i], 1.0e-9) / 0.30, 0.27),
                    0.0, 1.22);
                const auto underGrip = std::clamp(
                    (gripUtilization - 0.80) / 0.90, 0.0, 1.0);
                const auto instabilityScale = contacts[i].sticking
                    ? 1.0
                    : 1.0 + 0.78 * underGrip;
                const auto bowSpeedScale = std::clamp(
                    std::sqrt(std::abs(bowSpeed) / 0.45), 0.22, 1.25);
                const auto temperatureScale = std::clamp(
                    0.78
                        + 0.007
                            * (contacts[i].contactTemperatureC() - 20.0),
                    0.72, 1.20);
                const auto positionLevel = 0.86 + 0.18 * pos;

                const auto targetEnvelope =
                    rosinNoiseScale
                    * contactActivity
                    * forceScale
                    * instabilityScale
                    * bowSpeedScale
                    * temperatureScale
                    * positionLevel;
                const auto envelopeTime = targetEnvelope > rosinNoiseEnvelope[i]
                    ? 0.00045 : 0.0030;
                const auto envelopeAlpha =
                    1.0 - std::exp(-1.0 / (sampleRate * envelopeTime));
                rosinNoiseEnvelope[i] += envelopeAlpha
                    * (targetEnvelope - rosinNoiseEnvelope[i]);

                const auto transitionDecay =
                    std::exp(-1.0 / (sampleRate * 0.00014));
                rosinTransitionEnvelope[i] *= transitionDecay;

                const auto transitionTexture =
                    (1.0 - brightness) * 0.52 * rawNoise
                    + brightness * 0.62 * differentiated;
                const auto transitionVelocity =
                    0.0000060
                    * rosinTransitionVelocityScale[i]
                    * rosinNoiseScale
                    * rosinTransitionEnvelope[i]
                    * forceScale
                    * std::clamp(
                        0.55 + 0.65 * bowSpeedScale,
                        0.55, 1.35)
                    * (0.84 + 0.28 * pos)
                    * transitionTexture;

                // The roughness field perturbs the nonlinear friction solve;
                // a residual contact velocity captures microscopic hair/rosin
                // texture as well. Removing this path entirely regressed
                // physical string identity and material-colour separation.
                // Preserve the validated contact path while diagnosing the
                // reported apparent two-layer sound using isolated probes.
                rosinNoiseVelocity =
                    0.0000032
                    * rosinNoiseEnvelope[i]
                    * colouredNoise
                    + transitionVelocity;
                injection += rosinNoiseVelocity;
            }
            else
            {
                contacts[i].relax(sampleRate, contactStateRateScale);
                rosinNoisePrevious[i] *= 0.98;
                rosinNoiseEnvelope[i] *= std::exp(
                    -1.0 / (sampleRate * 0.004));
                rosinTransitionEnvelope[i] *= std::exp(
                    -1.0 / (sampleRate * 0.00014));
            }

            injection += chopImpactInjection[i];
            debug.bowInjectionVelocityMps[i] =
                static_cast<float>(injection);

            toBridge[i].write(incomingNut[i] + injection);
            toNut[i].write(incomingBridge[i] + injection);
            fromBridge[i].write(reflectedBridge);
            fromNut[i].write(reflectedNut);

            debug.contactNormalForceN[i] = static_cast<float>(bowForce[i]);
            debug.speakingFrequencyHz[i] = static_cast<float>(currentFrequency[i]);
            debug.sticking[i] = contacts[i].sticking;
            debug.contactTemperatureC[i] =
                static_cast<float>(contacts[i].contactTemperatureC());
            debug.contactGripUtilization[i] =
                static_cast<float>(contacts[i].gripUtilization());
            debug.contactSlipSpeedMps[i] =
                static_cast<float>(contacts[i].slipSpeedMps());
            debug.contactFrictionForceN[i] =
                static_cast<float>(contacts[i].lastFrictionForceN);
            debug.contactStaticFallback[i] =
                contacts[i].usedStaticFallback;
            debug.rosinNoiseVelocityMps[i] =
                static_cast<float>(rosinNoiseVelocity);
            debug.rosinTransitionEnvelope[i] =
                static_cast<float>(rosinTransitionEnvelope[i]);
            debug.rosinSurfaceCoordinate[i] =
                static_cast<float>(rosinSurfaceCoordinate[i]);
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
        debug.bridgeRockingVelocity =
            static_cast<float>(bridgeRockingVelocity);
        debug.vibratoOffsetCents = static_cast<float>(appliedVibratoCents);
        debug.strokeBiteGain = static_cast<float>(strokeBiteGain);
        debug.reversalAccelerationGain =
            static_cast<float>(reversalAccelerationGain);
        debug.oneShotLiftGain =
            static_cast<float>(oneShotLiftGain);
        debug.chopImpactVelocityMps =
            static_cast<float>(chopImpactVelocity);
        debug.oneShotActive =
            shortStrokeSamplesRemaining > 0
            || oneShotReleaseSamplesRemaining > 0
            || chopImpactSamplesRemaining > 0
            || chopDampingSamplesRemaining > 0;
        debug.bowDirection = bowDirection;
        debug.bowPairLowerString = pairLower;
        debug.primaryString = primaryString;

        // Listening/output calibration only; not part of the mechanical closure.
        // Directional radiation creates a small natural stereo side signal
        // without duplicating or detuning the string/body mechanics. Rocking
        // radiation is intentionally narrower at long wavelengths and slightly
        // stronger at short wavelengths, matching the increasing directivity of
        // a small resonant body as frequency rises.
        // Retain only a minority of the broadband mechanical coordinate in
        // the acoustic path. Modal motion carries the characteristic violin
        // body colour, while the residual direct path preserves attack and
        // prevents an unrealistically hollow modal-only sound.
        constexpr double broadbandRadiationFraction = 0.32;
        constexpr double rockingBroadbandRadiationFraction = 0.45;
        const auto radiatingBridgeVelocity =
            bodyModalVelocity
            + broadbandRadiationFraction
                * (bridgeVelocity - bodyModalVelocity);
        const auto radiatingRockingVelocity =
            rockingModalVelocity
            + rockingBroadbandRadiationFraction
                * (bridgeRockingVelocity - rockingModalVelocity);

        rockingRadiationLow += rockingRadiationAlpha
            * (radiatingRockingVelocity - rockingRadiationLow);
        const auto rockingRadiationHigh =
            radiatingRockingVelocity - rockingRadiationLow;
        constexpr double lowBandRockingMix = 0.16;
        constexpr double highBandRockingMix = 0.24;
        const auto directionalRocking =
            lowBandRockingMix * rockingRadiationLow
            + highBandRockingMix * rockingRadiationHigh;

        // Listening/output calibration only; the low-body-mode
        // rebalance increased the loudest double-stop by about 0.5%. Keep the
        // physical mechanics untouched and recover the previous headroom with
        // a sub-0.05 dB post-model calibration trim.
        constexpr double radiationCalibration = 17.90;
        return {
            radiationCalibration * radiationLeft.process(
                radiatingBridgeVelocity + directionalRocking),
            radiationCalibration * radiationRight.process(
                radiatingBridgeVelocity - directionalRocking)
        };
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
void FiddleEngine::primeUncontactedBowGesture(float preparationSeconds) noexcept
{
    impl_->primeUncontactedBowGesture(preparationSeconds);
}

void FiddleEngine::setStrokeBite(float amount, float durationSeconds) noexcept
{
    impl_->setStrokeBite(amount, durationSeconds);
}
void FiddleEngine::startBow(int direction) noexcept { impl_->startBow(direction); }
void FiddleEngine::startShortStroke(int direction,
                                    float durationSeconds,
                                    float liftDurationSeconds,
                                    float liftBrake,
                                    float liftForceCurve) noexcept
{
    impl_->startShortStroke(
        direction,
        durationSeconds,
        liftDurationSeconds,
        liftBrake,
        liftForceCurve);
}
void FiddleEngine::startChop(int direction,
                             float durationSeconds,
                             float impactVelocityMps,
                             float impactDurationSeconds) noexcept
{
    impl_->startChop(
        direction,
        durationSeconds,
        impactVelocityMps,
        impactDurationSeconds);
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
        const auto sample = impl_->processSample();
        left[i] += centerGain * static_cast<float>(sample[0]);
        right[i] += centerGain * static_cast<float>(sample[1]);
    }
}

DebugState FiddleEngine::debugSnapshot() const noexcept { return impl_->debug; }
} // namespace fiddle
