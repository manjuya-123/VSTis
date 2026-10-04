#include "Dsp/FiddleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

struct RenderResult
{
    std::vector<float> audio;
    fiddle::DebugState debug;
};

struct ContactTextureMetrics
{
    double noiseRms = 0.0;
    double stickingNoiseRms = 0.0;
    double brightnessRatio = 0.0;
    double maxGripUtilization = 0.0;
    double slidingFraction = 0.0;
    double transitionEnvelopePeak = 0.0;
    std::size_t transitionEnvelopeSamples = 0;
    std::size_t nearYieldStickSamples = 0;
    std::size_t stickSlipTransitions = 0;
};

fiddle::Controls controls()
{
    fiddle::Controls c;
    c.pressure = 0.56f;
    c.speed = 0.64f;
    c.attack = 0.62f;
    c.position = 0.48f;
    c.balance = -0.82f;
    return c;
}

RenderResult render(const fiddle::MaterialSettings& materials,
                    double seconds = 0.9)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);
    engine.setMaterials(materials);
    engine.setControls(controls());
    engine.beginBowStroke(false);
    engine.noteOn(329.6276f, 0.86f);

    RenderResult result;
    result.audio.assign(static_cast<std::size_t>(seconds * sampleRate), 0.0f);
    std::vector<float> right(result.audio.size(), 0.0f);
    engine.process(result.audio.data(), right.data(), result.audio.size());
    result.debug = engine.debugSnapshot();
    return result;
}

double differenceRms(const std::vector<float>& a, const std::vector<float>& b)
{
    const auto n = std::min(a.size(), b.size());
    if (n == 0)
        return 0.0;

    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(n));
}

double maxRosinNoiseVelocity(fiddle::ContactMaterialPreset preset)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::MaterialSettings materials;
    materials.contact = preset;
    engine.setMaterials(materials);
    engine.setControls(controls());
    engine.beginBowStroke(false);
    engine.noteOn(329.6276f, 0.86f);

    double maximum = 0.0;
    const auto samples = static_cast<std::size_t>(0.55 * sampleRate);
    for (std::size_t sample = 0; sample < samples; ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);

        const auto debug = engine.debugSnapshot();
        for (const auto noise : debug.rosinNoiseVelocityMps)
            maximum = std::max(maximum, std::abs(static_cast<double>(noise)));
    }

    return maximum;
}

ContactTextureMetrics contactTexture(
    fiddle::ContactMaterialPreset preset,
    float pressure = 0.44f,
    float speed = 0.60f,
    float position = 0.48f)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::MaterialSettings materials;
    materials.contact = preset;
    engine.setMaterials(materials);

    auto c = controls();
    c.pressure = pressure;
    c.speed = speed;
    c.position = position;
    engine.setControls(c);
    engine.beginBowStroke(false);
    engine.noteOn(329.6276f, 0.86f);

    double noiseEnergy = 0.0;
    double stickingNoiseEnergy = 0.0;
    double aggregateNoiseEnergy = 0.0;
    double aggregateNoiseDeltaEnergy = 0.0;
    double previousAggregateNoise = 0.0;
    std::size_t observedContacts = 0;
    std::size_t aggregateNoiseSamples = 0;
    std::size_t stickingNoiseSamples = 0;
    std::size_t slidingSamples = 0;
    bool havePreviousAggregateNoise = false;
    std::array<bool, 4> previousSticking {};
    std::array<bool, 4> havePreviousSticking {};

    ContactTextureMetrics result;
    const auto samples = static_cast<std::size_t>(0.55 * sampleRate);
    const auto warmup = static_cast<std::size_t>(0.060 * sampleRate);

    for (std::size_t sample = 0; sample < samples; ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);

        if (sample < warmup)
            continue;

        const auto debug = engine.debugSnapshot();
        double aggregateNoise = 0.0;
        bool observedThisSample = false;
        for (std::size_t i = 0; i < debug.contactNormalForceN.size(); ++i)
        {
            if (debug.contactNormalForceN[i] <= 1.0e-6f)
                continue;

            ++observedContacts;
            observedThisSample = true;
            const auto noise =
                static_cast<double>(debug.rosinNoiseVelocityMps[i]);
            aggregateNoise += noise;
            const auto grip =
                static_cast<double>(debug.contactGripUtilization[i]);
            const auto transitionEnvelope =
                static_cast<double>(debug.rosinTransitionEnvelope[i]);
            result.transitionEnvelopePeak =
                std::max(result.transitionEnvelopePeak, transitionEnvelope);
            if (transitionEnvelope >= 0.05)
                ++result.transitionEnvelopeSamples;
            noiseEnergy += noise * noise;

            if (havePreviousSticking[i]
                && previousSticking[i] != debug.sticking[i])
                ++result.stickSlipTransitions;
            previousSticking[i] = debug.sticking[i];
            havePreviousSticking[i] = true;
            result.maxGripUtilization =
                std::max(result.maxGripUtilization, grip);

            if (debug.sticking[i])
            {
                if (grip >= 0.68)
                {
                    ++result.nearYieldStickSamples;
                    stickingNoiseEnergy += noise * noise;
                    ++stickingNoiseSamples;
                }
            }
            else
            {
                ++slidingSamples;
            }
        }

        if (observedThisSample)
        {
            aggregateNoiseEnergy += aggregateNoise * aggregateNoise;
            if (havePreviousAggregateNoise)
            {
                const auto delta =
                    aggregateNoise - previousAggregateNoise;
                aggregateNoiseDeltaEnergy += delta * delta;
            }
            previousAggregateNoise = aggregateNoise;
            havePreviousAggregateNoise = true;
            ++aggregateNoiseSamples;
        }
    }

    if (observedContacts > 0)
    {
        result.noiseRms = std::sqrt(
            noiseEnergy / static_cast<double>(observedContacts));
        result.slidingFraction =
            static_cast<double>(slidingSamples)
            / static_cast<double>(observedContacts);
    }

    if (stickingNoiseSamples > 0)
        result.stickingNoiseRms = std::sqrt(
            stickingNoiseEnergy
            / static_cast<double>(stickingNoiseSamples));

    if (aggregateNoiseSamples > 1)
    {
        const auto aggregateRms = std::sqrt(
            aggregateNoiseEnergy
            / static_cast<double>(aggregateNoiseSamples));
        const auto deltaRms = std::sqrt(
            aggregateNoiseDeltaEnergy
            / static_cast<double>(aggregateNoiseSamples - 1));
        result.brightnessRatio =
            deltaRms / (aggregateRms + 1.0e-30);
    }

    return result;
}

double reversalSpeed(fiddle::BowStickPreset preset)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::MaterialSettings materials;
    materials.bowStick = preset;
    engine.setMaterials(materials);

    auto c = controls();
    c.attack = 0.62f;
    engine.setControls(c);

    engine.beginBowStroke(true);
    engine.noteOn(329.6276f, 0.9f);

    std::vector<float> left(static_cast<std::size_t>(0.16 * sampleRate), 0.0f);
    std::vector<float> right(left.size(), 0.0f);
    engine.process(left.data(), right.data(), left.size());

    engine.beginBowStroke(true);
    std::vector<float> reverse(static_cast<std::size_t>(0.022 * sampleRate), 0.0f);
    std::vector<float> reverseR(reverse.size(), 0.0f);
    engine.process(reverse.data(), reverseR.data(), reverse.size());

    return engine.debugSnapshot().bowSpeedMps;
}

int fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main()
{
    fiddle::MaterialSettings traditional;

    auto rigid = traditional;
    rigid.body = fiddle::BodyMaterialPreset::RigidComposite;

    const auto traditionalBody = render(traditional);
    const auto rigidBody = render(rigid);
    const auto bodyDifference = differenceRms(
        traditionalBody.audio, rigidBody.audio);

    if (!std::isfinite(bodyDifference) || bodyDifference < 1.0e-5)
        return fail("Body material profile did not audibly change the body response");

    auto dry = traditional;
    dry.contact = fiddle::ContactMaterialPreset::DryLightGrip;
    auto grippy = traditional;
    grippy.contact = fiddle::ContactMaterialPreset::HighGripRosin;

    const auto dryContact = render(dry);
    const auto grippyContact = render(grippy);
    const auto contactDifference = differenceRms(
        dryContact.audio, grippyContact.audio);

    if (!std::isfinite(contactDifference) || contactDifference < 1.0e-5)
        return fail("Hair/Rosin material profile did not change bow-string interaction");

    const auto mediumRosinNoise = maxRosinNoiseVelocity(
        fiddle::ContactMaterialPreset::HorsehairMediumRosin);
    const auto dryRosinNoise = maxRosinNoiseVelocity(
        fiddle::ContactMaterialPreset::DryLightGrip);
    const auto highGripRosinNoise = maxRosinNoiseVelocity(
        fiddle::ContactMaterialPreset::HighGripRosin);

    if (!(mediumRosinNoise > 1.0e-8
          && dryRosinNoise > 1.0e-8
          && dryRosinNoise > highGripRosinNoise))
        return fail("Microscopic rosin roughness is missing or ignores contact material");

    const auto mediumTexture = contactTexture(
        fiddle::ContactMaterialPreset::HorsehairMediumRosin);
    const auto dryTexture = contactTexture(
        fiddle::ContactMaterialPreset::DryLightGrip);
    const auto highGripTexture = contactTexture(
        fiddle::ContactMaterialPreset::HighGripRosin);

    if (!(mediumTexture.noiseRms > 1.0e-9
          && dryTexture.noiseRms >= 1.70 * mediumTexture.noiseRms
          && highGripTexture.noiseRms <= 0.85 * mediumTexture.noiseRms))
        return fail("Rosin roughness RMS no longer follows Dry / Medium / High-Grip material intent");

    if (!(mediumTexture.nearYieldStickSamples > 0
          && mediumTexture.stickingNoiseRms > 1.0e-10
          && mediumTexture.maxGripUtilization >= 0.68))
        return fail("Near-yield sticking no longer produces microscopic pre-slip roughness");

    if (!(mediumTexture.stickSlipTransitions > 0
          && mediumTexture.transitionEnvelopePeak >= 0.50
          && mediumTexture.transitionEnvelopeSamples > 1000
          && mediumTexture.transitionEnvelopeSamples < 16000))
        return fail("Stick-slip rosin burst is missing or has become effectively continuous");

    const auto fingerboardTexture = contactTexture(
        fiddle::ContactMaterialPreset::HorsehairMediumRosin,
        0.44f, 0.60f, 0.10f);
    const auto bridgeTexture = contactTexture(
        fiddle::ContactMaterialPreset::HorsehairMediumRosin,
        0.44f, 0.60f, 0.90f);

    if (!(fingerboardTexture.brightnessRatio > 0.0
          && bridgeTexture.brightnessRatio
             > fingerboardTexture.brightnessRatio * 1.08))
        return fail("Bow Contact no longer makes microscopic rosin texture brighter toward bridge");

    auto steel = traditional;
    steel.strings = fiddle::StringCorePreset::SteelCore;
    auto gut = traditional;
    gut.strings = fiddle::StringCorePreset::GutLike;

    const auto steelStrings = render(steel);
    const auto gutStrings = render(gut);
    const auto stringDifference = differenceRms(
        steelStrings.audio, gutStrings.audio);

    if (!std::isfinite(stringDifference) || stringDifference < 1.0e-5)
        return fail("String-core material profile did not change string-loop behavior");

    const auto rigidBowSpeed =
        reversalSpeed(fiddle::BowStickPreset::LightRigidExperimental);
    const auto flexibleBowSpeed =
        reversalSpeed(fiddle::BowStickPreset::FlexibleExperimental);

    // 22 ms after commanding the up-bow, the light/rigid profile should have
    // progressed farther in the negative direction than the flexible profile.
    if (!(rigidBowSpeed < flexibleBowSpeed - 0.02))
    {
        std::cerr << "rigid_speed=" << rigidBowSpeed
                  << " flexible_speed=" << flexibleBowSpeed << '\n';
        return fail("Bow stick material did not change reversal response");
    }

    std::cout << "PASS\n"
              << "body_difference_rms=" << bodyDifference << '\n'
              << "contact_difference_rms=" << contactDifference << '\n'
              << "medium_rosin_noise_velocity=" << mediumRosinNoise << '\n'
              << "dry_rosin_noise_velocity=" << dryRosinNoise << '\n'
              << "high_grip_rosin_noise_velocity=" << highGripRosinNoise << '\n'
              << "medium_texture_rms=" << mediumTexture.noiseRms << '\n'
              << "medium_sticking_texture_rms=" << mediumTexture.stickingNoiseRms << '\n'
              << "medium_max_grip_utilization=" << mediumTexture.maxGripUtilization << '\n'
              << "medium_near_yield_stick_samples=" << mediumTexture.nearYieldStickSamples << '\n'
              << "medium_sliding_fraction=" << mediumTexture.slidingFraction << '\n'
              << "medium_stick_slip_transitions="
              << mediumTexture.stickSlipTransitions << '\n'
              << "medium_transition_envelope_peak="
              << mediumTexture.transitionEnvelopePeak << '\n'
              << "medium_transition_envelope_samples="
              << mediumTexture.transitionEnvelopeSamples << '\n'
              << "fingerboard_texture_brightness="
              << fingerboardTexture.brightnessRatio << '\n'
              << "bridge_texture_brightness="
              << bridgeTexture.brightnessRatio << '\n'
              << "dry_texture_rms=" << dryTexture.noiseRms << '\n'
              << "high_grip_texture_rms=" << highGripTexture.noiseRms << '\n'
              << "string_difference_rms=" << stringDifference << '\n'
              << "rigid_bow_speed=" << rigidBowSpeed << '\n'
              << "flexible_bow_speed=" << flexibleBowSpeed << '\n';

    return EXIT_SUCCESS;
}
