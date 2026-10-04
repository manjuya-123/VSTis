#include "Dsp/FiddleEngine.h"

#include <algorithm>
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

    if (!(mediumRosinNoise > 1.0e-7
          && dryRosinNoise > 1.0e-7
          && dryRosinNoise > highGripRosinNoise))
        return fail("Microscopic rosin roughness is missing or ignores contact material");

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
              << "string_difference_rms=" << stringDifference << '\n'
              << "rigid_bow_speed=" << rigidBowSpeed << '\n'
              << "flexible_bow_speed=" << flexibleBowSpeed << '\n';

    return EXIT_SUCCESS;
}
