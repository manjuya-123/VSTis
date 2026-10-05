#include "Dsp/FiddleEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

bool finiteBuffer(const std::vector<float>& x)
{
    return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}

double rms(const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    begin = std::min(begin, x.size());
    end = std::min(end, x.size());
    if (end <= begin)
        return 0.0;

    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return std::sqrt(sum / static_cast<double>(end - begin));
}

void render(fiddle::FiddleEngine& engine,
            std::vector<float>& left,
            std::vector<float>& right,
            std::size_t offset,
            std::size_t count)
{
    engine.process(left.data() + offset, right.data() + offset, count);
}

double maxBridgeRocking(float noteHz, float balance)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.55f;
    controls.speed = 0.60f;
    controls.attack = 0.55f;
    controls.position = 0.45f;
    controls.balance = balance;
    engine.setControls(controls);
    engine.beginBowStroke(false);
    engine.noteOn(noteHz, 0.85f);

    double maximum = 0.0;
    const auto samples = static_cast<std::size_t>(0.45 * sampleRate);
    const auto warmup = static_cast<std::size_t>(0.06 * sampleRate);
    for (std::size_t i = 0; i < samples; ++i)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);
        if (i >= warmup)
        {
            maximum = std::max(
                maximum,
                std::abs(static_cast<double>(
                    engine.debugSnapshot().bridgeRockingVelocity)));
        }
    }
    return maximum;
}

int fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main()
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.55f;
    controls.speed = 0.60f;
    controls.attack = 0.55f;
    controls.position = 0.45f;
    controls.balance = 0.0f;
    controls.vibratoWidth = 0.0f;
    controls.vibratoPace = 0.5f;
    engine.setControls(controls);

    constexpr std::size_t sustainSamples = static_cast<std::size_t>(sampleRate * 1.5);
    constexpr std::size_t releaseSamples = static_cast<std::size_t>(sampleRate * 1.0);
    std::vector<float> left(sustainSamples + releaseSamples, 0.0f);
    std::vector<float> right(left.size(), 0.0f);

    // E4 is naturally assigned to the D+A pair: fingered D plus open-A drone.
    engine.noteOn(329.6276f, 0.85f);
    render(engine, left, right, 0, sustainSamples);

    auto debug = engine.debugSnapshot();
    if (debug.primaryString != 1 || debug.bowPairLowerString != 1)
        return fail("E4 should use the D+A pair");

    if (!(debug.contactNormalForceN[1] > 0.0f && debug.contactNormalForceN[2] > 0.0f))
        return fail("balanced D+A bow should apply force to both strings");

    if (!(debug.bowAngleDeg > 0.20f && debug.bowAngleDeg < 0.45f))
        return fail("Balance center should pressure-compensate the physical bow angle");

    const auto normalizedD =
        debug.contactNormalForceN[1] / 0.23878228245098557f;
    const auto normalizedA =
        debug.contactNormalForceN[2] / 0.19054878048780488f;
    const auto normalizedMismatch =
        std::abs(normalizedD - normalizedA) / std::max(normalizedD, normalizedA);
    if (normalizedMismatch > 0.025f)
        return fail("Balance center should keep normalized D/A pressure nearly equal");

    const auto forceSum = debug.contactNormalForceN[1] + debug.contactNormalForceN[2];
    if (!(forceSum > 0.05f && forceSum < 0.60f))
        return fail("normal-force mapping is outside the expected physical range");

    const auto hotContact = std::max(
        debug.contactTemperatureC[1], debug.contactTemperatureC[2]);
    if (!(hotContact > 21.0f && hotContact < 85.0f))
        return fail("thermal rosin state did not heat during sustained bowing");

    if (!finiteBuffer(left) || !finiteBuffer(right))
        return fail("non-finite audio sample detected");

    const auto sustainRms = rms(left,
                                static_cast<std::size_t>(0.65 * sampleRate),
                                static_cast<std::size_t>(1.35 * sampleRate));
    if (sustainRms < 1.0e-5)
        return fail("engine produced effectively silent sustain");

    engine.noteOff();
    render(engine, left, right, sustainSamples, releaseSamples);

    const auto tailRms = rms(left,
                             sustainSamples + static_cast<std::size_t>(0.75 * sampleRate),
                             sustainSamples + releaseSamples);
    if (!(tailRms < sustainRms * 0.55))
        return fail("release tail did not decay enough");

    const auto cooledDebug = engine.debugSnapshot();
    const auto cooledContact = std::max(
        cooledDebug.contactTemperatureC[1], cooledDebug.contactTemperatureC[2]);
    if (!(cooledContact < hotContact - 0.5f))
        return fail("thermal rosin state did not cool after bow release");

    engine.reset();
    engine.setControls(controls);
    controls.balance = -0.85f;
    engine.setControls(controls);
    engine.noteOn(329.6276f, 0.85f);
    std::vector<float> scratch(static_cast<std::size_t>(0.7 * sampleRate), 0.0f);
    std::vector<float> scratchR(scratch.size(), 0.0f);
    engine.process(scratch.data(), scratchR.data(), scratch.size());
    const auto dHeavy = engine.debugSnapshot();

    engine.reset();
    controls.balance = +0.85f;
    engine.setControls(controls);
    engine.noteOn(329.6276f, 0.85f);
    std::fill(scratch.begin(), scratch.end(), 0.0f);
    std::fill(scratchR.begin(), scratchR.end(), 0.0f);
    engine.process(scratch.data(), scratchR.data(), scratch.size());
    const auto aHeavy = engine.debugSnapshot();

    if (!(dHeavy.contactNormalForceN[1] > dHeavy.contactNormalForceN[2]))
        return fail("negative Balance should favor lower/D string");
    if (!(aHeavy.contactNormalForceN[2] > aHeavy.contactNormalForceN[1]))
        return fail("positive Balance should favor upper/A string");

    // Exact String Focus endpoints are true single-string bow angles.
    engine.reset();
    controls.balance = -1.0f;
    engine.setControls(controls);
    engine.noteOn(440.0f, 0.85f);
    std::fill(scratch.begin(), scratch.end(), 0.0f);
    std::fill(scratchR.begin(), scratchR.end(), 0.0f);
    engine.process(scratch.data(), scratchR.data(), scratch.size());
    const auto isolatedD = engine.debugSnapshot();
    const auto isolatedDPair =
        isolatedD.contactNormalForceN[1] + isolatedD.contactNormalForceN[2];
    if (!(isolatedDPair > 0.0f)
        || isolatedD.contactNormalForceN[2] > isolatedDPair * 0.001f)
        return fail("String Focus -1 should physically clear the adjacent A string");

    engine.reset();
    controls.balance = +1.0f;
    engine.setControls(controls);
    engine.noteOn(440.0f, 0.85f);
    std::fill(scratch.begin(), scratch.end(), 0.0f);
    std::fill(scratchR.begin(), scratchR.end(), 0.0f);
    engine.process(scratch.data(), scratchR.data(), scratch.size());
    const auto isolatedA = engine.debugSnapshot();
    const auto isolatedAPair =
        isolatedA.contactNormalForceN[1] + isolatedA.contactNormalForceN[2];
    if (!(isolatedAPair > 0.0f)
        || isolatedA.contactNormalForceN[1] > isolatedAPair * 0.001f)
        return fail("String Focus +1 should physically clear the adjacent D string");

    const auto gStringRocking = maxBridgeRocking(195.9977f, -0.95f);
    const auto eStringRocking = maxBridgeRocking(659.2551f, +0.95f);
    if (!(gStringRocking > 1.0e-6 && eStringRocking > 1.0e-6))
        return fail("outer strings no longer excite bridge rocking mobility");

    engine.reset();
    controls.balance = 0.0f;
    controls.vibratoWidth = 1.0f;
    controls.vibratoPace = 0.5f;
    engine.setControls(controls);
    engine.noteOn(440.0f, 0.85f);
    std::fill(scratch.begin(), scratch.end(), 0.0f);
    std::fill(scratchR.begin(), scratchR.end(), 0.0f);
    engine.process(scratch.data(), scratchR.data(), scratch.size());
    const auto openA = engine.debugSnapshot();
    if (std::abs(openA.vibratoOffsetCents) > 0.001f)
        return fail("open string should not receive left-hand vibrato");

    engine.reset();
    engine.setControls(controls);
    engine.noteOn(493.8833f, 0.85f); // B4, stopped on A string
    std::fill(scratch.begin(), scratch.end(), 0.0f);
    std::fill(scratchR.begin(), scratchR.end(), 0.0f);
    engine.process(scratch.data(), scratchR.data(), scratch.size());
    const auto stoppedA = engine.debugSnapshot();
    if (std::abs(stoppedA.vibratoOffsetCents) < 1.0f)
        return fail("stopped string should receive vibrato");

    std::cout << "PASS\n"
              << "sustain_rms=" << sustainRms << '\n'
              << "tail_rms=" << tailRms << '\n'
              << "balanced_D_force_N=" << debug.contactNormalForceN[1] << '\n'
              << "balanced_A_force_N=" << debug.contactNormalForceN[2] << '\n'
              << "bow_speed_mps=" << debug.bowSpeedMps << '\n'
              << "bow_angle_deg=" << debug.bowAngleDeg << '\n'
              << "hot_contact_c=" << hotContact << '\n'
              << "cooled_contact_c=" << cooledContact << '\n'
              << "g_string_rocking_peak_mps=" << gStringRocking << '\n'
              << "e_string_rocking_peak_mps=" << eStringRocking << '\n';

    return EXIT_SUCCESS;
}
