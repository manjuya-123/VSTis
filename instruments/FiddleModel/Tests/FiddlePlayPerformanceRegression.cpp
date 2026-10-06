#include "Dsp/FiddleEngine.h"
#include "Dsp/FiddleFingeringVoicer.h"
#include "Dsp/FiddleGestureProfile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr float listenGain = 7.9432823f; // +18 dB, matches plugin default Output Level.


double correlationAtFrequency(const std::vector<float>& x,
                              std::size_t begin,
                              std::size_t end,
                              double frequency)
{
    begin = std::min(begin, x.size());
    end = std::min(end, x.size());
    if (frequency <= 0.0 || end <= begin + 100)
        return -1.0;

    const auto lag = sampleRate / frequency;
    const auto lagInt = static_cast<std::size_t>(std::floor(lag));
    const auto frac = lag - static_cast<double>(lagInt);
    if (begin + lagInt + 2 >= end)
        return -1.0;

    double mean = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        mean += x[i];
    mean /= static_cast<double>(end - begin);

    double dot = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    const auto last = end - lagInt - 1;
    for (std::size_t i = begin; i < last; ++i)
    {
        const auto a = static_cast<double>(x[i]) - mean;
        const auto d0 = static_cast<double>(x[i + lagInt]) - mean;
        const auto d1 = static_cast<double>(x[i + lagInt + 1]) - mean;
        const auto b = (1.0 - frac) * d0 + frac * d1;
        dot += a * b;
        aa += a * a;
        bb += b * b;
    }
    return dot / (std::sqrt(aa * bb) + 1.0e-30);
}

double estimateFrequencyNear(const std::vector<float>& x,
                             std::size_t begin,
                             std::size_t end,
                             double target)
{
    constexpr int candidates = 480;
    double bestFrequency = target;
    double bestCorrelation = -2.0;
    for (int i = 0; i <= candidates; ++i)
    {
        const auto fraction = static_cast<double>(i) / candidates;
        const auto frequency = target * (0.97 + 0.06 * fraction);
        const auto corr = correlationAtFrequency(
            x, begin, end, frequency);
        if (corr > bestCorrelation)
        {
            bestCorrelation = corr;
            bestFrequency = frequency;
        }
    }
    return bestFrequency;
}

double centsBetween(double measured, double target)
{
    return 1200.0 * std::log2(measured / target);
}

void writeU16(std::ofstream& out, std::uint16_t value)
{
    const char b[2] {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)
    };
    out.write(b, 2);
}

void writeU32(std::ofstream& out, std::uint32_t value)
{
    const char b[4] {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)
    };
    out.write(b, 4);
}

bool writeWav(const std::filesystem::path& path,
              const std::vector<float>& left,
              const std::vector<float>& right)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary);
    if (!out || left.size() != right.size())
        return false;

    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bits = 16;
    const auto frames = static_cast<std::uint32_t>(left.size());
    const auto bytes = frames * channels * (bits / 8u);

    out.write("RIFF", 4); writeU32(out, 36u + bytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4); writeU32(out, 16u);
    writeU16(out, 1u); writeU16(out, channels);
    writeU32(out, static_cast<std::uint32_t>(sampleRate));
    writeU32(out, static_cast<std::uint32_t>(sampleRate) * 4u);
    writeU16(out, 4u); writeU16(out, bits);
    out.write("data", 4); writeU32(out, bytes);

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const auto encode = [](float x)
        {
            x = std::clamp(x * listenGain, -1.0f, 1.0f);
            return static_cast<std::uint16_t>(
                static_cast<std::int16_t>(std::lrint(x * 32767.0f)));
        };
        writeU16(out, encode(left[i]));
        writeU16(out, encode(right[i]));
    }

    return static_cast<bool>(out);
}

void appendSilence(std::vector<float>& left,
                   std::vector<float>& right,
                   double seconds)
{
    const auto count = static_cast<std::size_t>(seconds * sampleRate);
    left.insert(left.end(), count, 0.0f);
    right.insert(right.end(), count, 0.0f);
}

void render(fiddle::FiddleEngine& engine,
            std::vector<float>& left,
            std::vector<float>& right,
            double seconds)
{
    const auto count = static_cast<std::size_t>(seconds * sampleRate);
    const auto offset = left.size();
    left.resize(offset + count, 0.0f);
    right.resize(offset + count, 0.0f);
    engine.process(left.data() + offset, right.data() + offset, count);
}

int fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main(int argc, char** argv)
{
    fiddle::FiddleEngine engine;
    engine.prepare(sampleRate);

    fiddle::Controls controls;
    controls.pressure = 0.56f;
    controls.speed = 0.66f;
    controls.attack = 0.78f;
    controls.position = 0.48f;
    controls.balance = -0.45f;
    controls.vibratoWidth = 0.0f;
    engine.setControls(controls);

    // Physical left-hand shape: E4 on D string + B4 on A string.
    std::array<float, 4> fingering {};
    fingering[1] = 329.6276f;
    fingering[2] = 493.8833f;
    engine.setFingeringLayout(fingering, 1, 1, 0.88f);

    std::vector<float> left;
    std::vector<float> right;

    // Down bow.
    engine.startBow(+1);
    render(engine, left, right, 0.28);
    if (engine.debugSnapshot().bowDirection != 1)
        return fail("Down Bow did not keep positive bow direction");
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Up bow, same left hand.
    engine.startBow(-1);
    render(engine, left, right, 0.28);
    if (engine.debugSnapshot().bowDirection != -1)
        return fail("Up Bow did not keep negative bow direction");
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Slur: keep the bow moving while the left hand changes stopped positions.
    engine.startBow(+1);
    std::array<float, 4> slurFingering {};
    slurFingering[1] = 369.9944f; // F#4 on D
    slurFingering[2] = 554.3653f; // C#5 on A
    engine.setFingeringLayout(slurFingering, 1, 1, 0.88f);

    // A stopped-note change should move the physical speaking length quickly,
    // without an artificial tens-of-milliseconds portamento. Measure the
    // actual engine state while the bow continues in the same direction.
    constexpr auto maxSlurSettleSamples =
        static_cast<std::size_t>(0.008 * sampleRate);
    std::size_t slurSettleSamples = maxSlurSettleSamples + 1;
    for (std::size_t sample = 0; sample < maxSlurSettleSamples; ++sample)
    {
        float sampleLeft = 0.0f;
        float sampleRight = 0.0f;
        engine.process(&sampleLeft, &sampleRight, 1);
        left.push_back(sampleLeft);
        right.push_back(sampleRight);

        const auto state = engine.debugSnapshot();
        if (std::abs(state.speakingFrequencyHz[1] - 369.9944f) <= 2.0f
            && std::abs(state.speakingFrequencyHz[2] - 554.3653f) <= 2.0f)
        {
            slurSettleSamples = sample + 1;
            break;
        }
    }

    if (slurSettleSamples > maxSlurSettleSamples)
        return fail("Slur fingering did not settle within 8 ms");

    render(engine, left, right, 0.21);

    const auto slurDebug = engine.debugSnapshot();
    if (slurDebug.bowDirection != 1)
        return fail("Slur changed bow direction unexpectedly");
    if (std::abs(slurDebug.speakingFrequencyHz[1] - 369.9944f) > 2.0f
        || std::abs(slurDebug.speakingFrequencyHz[2] - 554.3653f) > 2.0f)
        return fail("Slur did not move the held fingering while bowing");
    std::cout << "slur_fingering_settle_ms="
              << (1000.0 * static_cast<double>(slurSettleSamples)
                  / sampleRate)
              << '\n';
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Audible monophonic slur pitch tracking. The UI uses the engine's
    // speaking-frequency state, so explicitly verify that the radiated output
    // follows the same stopped-note changes while one bow stroke continues.
    {
        fiddle::FiddleEngine mono;
        mono.prepare(sampleRate);

        fiddle::Controls monoControls;
        monoControls.pressure = 0.56f;
        monoControls.speed = 0.66f;
        monoControls.attack = 0.78f;
        monoControls.position = 0.48f;
        monoControls.balance = -0.95f;
        monoControls.singleStringIsolation = 1.0f;
        mono.setControls(monoControls);

        std::vector<float> monoLeft;
        std::vector<float> monoRight;
        std::array<float, 4> monoFingering {};

        constexpr std::array<float, 3> targets {
            329.6276f, 369.9944f, 391.9954f
        };
        std::array<double, targets.size()> measuredHz {};

        mono.startBow(+1);
        for (std::size_t noteIndex = 0; noteIndex < targets.size(); ++noteIndex)
        {
            monoFingering.fill(0.0f);
            monoFingering[1] = targets[noteIndex];
            mono.setFingeringLayout(monoFingering, 1, 1, 0.88f);

            const auto segmentBegin = monoLeft.size();
            render(mono, monoLeft, monoRight,
                   noteIndex == 0 ? 0.30 : 0.22);
            const auto segmentEnd = monoLeft.size();

            const auto measureLength =
                static_cast<std::size_t>(0.11 * sampleRate);
            const auto measureBegin =
                segmentEnd > measureLength
                    ? std::max(segmentBegin, segmentEnd - measureLength)
                    : segmentBegin;
            measuredHz[noteIndex] = estimateFrequencyNear(
                monoLeft, measureBegin, segmentEnd, targets[noteIndex]);

            const auto cents =
                centsBetween(measuredHz[noteIndex], targets[noteIndex]);
            std::cout << "monophonic_slur_pitch"
                      << " target=" << targets[noteIndex]
                      << " measured=" << measuredHz[noteIndex]
                      << " cents=" << cents << '\n';
            if (std::abs(cents) > 5.0)
                return fail("Monophonic audible slur pitch did not follow fingering");
        }

        if (!(measuredHz[1] > measuredHz[0] * 1.08
              && measuredHz[2] > measuredHz[1] * 1.04))
            return fail("Monophonic audible slur pitch stayed effectively fixed");

        mono.stopBow();
        render(mono, monoLeft, monoRight, 0.10);

        if (argc >= 2)
        {
            const auto monoOutput =
                std::filesystem::path(argv[1]).parent_path()
                / "18_monophonic_pitch_slur.wav";
            if (!writeWav(monoOutput, monoLeft, monoRight))
                return fail("Could not write monophonic pitch-slur WAV");
        }
    }

    // Monophonic Fiddle Play auto-focus: one stopped E4 on D should
    // primarily bow D, not silently turn every melody note into a D+A drone.
    std::array<float, 4> singleFingering {};
    singleFingering[1] = 329.6276f;
    engine.setFingeringLayout(singleFingering, 1, 1, 0.88f);
    std::array<int, 4> singleNotes { 64, -1, -1, -1 };
    const auto singleLayout =
        fiddle::voiceFingering(singleNotes, 1, 64);
    controls.balance =
        fiddle::singleStringFocusForLayout(singleLayout, 1);
    controls.singleStringIsolation = 1.0f;
    engine.setControls(controls);
    engine.startBow(+1);
    const auto singleFocusBegin = left.size();
    render(engine, left, right, 0.30);
    const auto singleFocusEnd = left.size();

    const auto singleFocus = engine.debugSnapshot();
    const auto singlePairForce =
        singleFocus.contactNormalForceN[1]
        + singleFocus.contactNormalForceN[2];
    if (!(singlePairForce > 0.001f)
        || singleFocus.contactNormalForceN[2] > singlePairForce * 0.0001f)
        return fail("Monophonic Fiddle Play must not directly bow adjacent A string");
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Open-string Drone Bow deliberately re-centres the same stopped D + open A
    // shape so both strings are directly contacted.
    controls.balance = 0.0f;
    controls.singleStringIsolation = 0.0f;
    engine.setControls(controls);
    engine.startBow(+1);
    const auto droneBegin = left.size();
    render(engine, left, right, 0.30);
    const auto droneEnd = left.size();

    const auto openDrone = engine.debugSnapshot();
    if (!(openDrone.contactNormalForceN[1] > 0.001f
          && openDrone.contactNormalForceN[2] > 0.001f))
        return fail("Open-string Drone Bow did not contact both D and A strings");
    if (std::abs(openDrone.speakingFrequencyHz[1] - 329.6276f) > 1.5f
        || std::abs(openDrone.speakingFrequencyHz[2] - 440.0f) > 1.0f)
        return fail("Open-string Drone Bow did not preserve stopped D + open A tuning");
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Restore the original D/A fingering for the remaining gestures.
    controls.balance = -0.45f;
    controls.pressure = 0.56f;
    controls.speed = 0.66f;
    controls.attack = 0.78f;
    engine.setControls(controls);
    engine.setFingeringLayout(fingering, 1, 1, 0.88f);

    const auto gestureBaseControls = controls;
    const auto applyGesture =
        [&](fiddle::BowAction action, float velocity)
        {
            const auto profile =
                fiddle::makeBowGestureProfile(action, velocity);
            auto gestureControls = gestureBaseControls;
            gestureControls.pressure = std::clamp(
                gestureControls.pressure + profile.pressureBoost,
                0.0f, 1.0f);
            gestureControls.speed = std::clamp(
                gestureControls.speed * profile.speedScale,
                0.0f, 1.0f);
            gestureControls.attack = std::clamp(
                gestureControls.attack + profile.responseBoost,
                0.0f, 1.0f);
            engine.setControls(gestureControls);
            engine.setStrokeBite(
                profile.biteBoost, profile.biteDurationSeconds);
            return profile;
        };

    // Short stroke: compact, articulate, but not as forceful as Accent.
    const auto shortProfile =
        applyGesture(fiddle::BowAction::ShortStroke, 0.82f);
    const auto shortBegin = left.size();
    engine.startShortStroke(
        +1,
        shortProfile.durationSeconds,
        shortProfile.liftDurationSeconds,
        shortProfile.liftBrake,
        shortProfile.liftForceCurve);
    render(engine, left, right, 0.18);
    const auto shortEnd = left.size();
    const auto afterShort = engine.debugSnapshot();
    const auto shortForce =
        afterShort.contactNormalForceN[1] + afterShort.contactNormalForceN[2];
    if (shortForce > 0.01f || afterShort.oneShotActive)
        return fail("Short Stroke did not complete its physical bow lift");
    render(engine, left, right, 0.06);

    // Accent: stronger first bite and shorter, more forceful one-shot.
    const auto accentProfile =
        applyGesture(fiddle::BowAction::AccentStroke, 0.82f);
    const auto accentBegin = left.size();
    engine.startShortStroke(
        -1,
        accentProfile.durationSeconds,
        accentProfile.liftDurationSeconds,
        accentProfile.liftBrake,
        accentProfile.liftForceCurve);
    render(engine, left, right, 0.15);
    const auto accentEnd = left.size();
    const auto afterAccent = engine.debugSnapshot();
    const auto accentForce =
        afterAccent.contactNormalForceN[1] + afterAccent.contactNormalForceN[2];
    if (accentForce > 0.015f || afterAccent.oneShotActive)
        return fail("Accent Stroke did not complete its physical bow lift");
    render(engine, left, right, 0.06);

    // Physical Chop: low travel/high force plus a contact-point collision.
    const auto chopProfile =
        applyGesture(fiddle::BowAction::Chop, 0.90f);
    const auto chopBegin = left.size();
    engine.startChop(
        +1,
        chopProfile.durationSeconds,
        chopProfile.impactVelocityMps,
        chopProfile.impactDurationSeconds);
    render(engine, left, right, 0.10);
    const auto chopEnd = left.size();
    const auto afterChop = engine.debugSnapshot();
    const auto chopForce =
        afterChop.contactNormalForceN[1] + afterChop.contactNormalForceN[2];
    if (chopForce > 0.015f || afterChop.oneShotActive)
        return fail("Chop surrogate did not complete its short bow lift");
    render(engine, left, right, 0.05);

    // Tremolo: light, even high-rate reversals.
    const auto tremoloProfile =
        applyGesture(fiddle::BowAction::Tremolo, 0.82f);
    const auto tremoloBegin = left.size();
    engine.startTremolo(tremoloProfile.tremoloReversalsPerSecond);
    auto tremoloDirection = engine.debugSnapshot().bowDirection;
    int tremoloReversals = 0;
    float tremoloMaxReCatch = 1.0f;
    for (int i = 0; i < 104; ++i)
    {
        render(engine, left, right, 0.005);
        const auto state = engine.debugSnapshot();
        tremoloMaxReCatch = std::max(tremoloMaxReCatch, state.strokeBiteGain);
        if (state.bowDirection != tremoloDirection)
        {
            ++tremoloReversals;
            tremoloDirection = state.bowDirection;
        }
    }
    if (tremoloReversals < 5)
        return fail("Tremolo did not produce repeated physical bow reversals");
    if (tremoloMaxReCatch < 1.02f)
        return fail("Tremolo reversals did not re-catch the string");
    const auto tremoloEnd = left.size();
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Fiddle shuffle: stronger long-short-short pulse with slower subdivisions.
    const auto shuffleProfile =
        applyGesture(fiddle::BowAction::Shuffle, 0.82f);
    const auto shuffleBegin = left.size();
    engine.startShuffle(shuffleProfile.shuffleSubdivisionsPerSecond);
    auto shuffleDirection = engine.debugSnapshot().bowDirection;
    int shuffleReversals = 0;
    float shuffleMaxReCatch = 1.0f;
    for (int i = 0; i < 116; ++i)
    {
        render(engine, left, right, 0.005);
        const auto state = engine.debugSnapshot();
        shuffleMaxReCatch = std::max(shuffleMaxReCatch, state.strokeBiteGain);
        if (state.bowDirection != shuffleDirection)
        {
            ++shuffleReversals;
            shuffleDirection = state.bowDirection;
        }
    }
    if (shuffleReversals < 4)
        return fail("Shuffle did not advance through repeated long-short-short bow reversals");
    if (shuffleMaxReCatch < 1.02f)
        return fail("Shuffle reversals did not re-catch the string");
    const auto shuffleEnd = left.size();
    engine.stopBow();
    render(engine, left, right, 0.08);

    // Drone/double-stop gesture: same fingering, even focus across D/A.
    controls = gestureBaseControls;
    controls.balance = 0.0f;
    engine.setControls(controls);
    engine.startBow(+1);
    render(engine, left, right, 0.38);
    const auto droneDebug = engine.debugSnapshot();
    if (!(droneDebug.contactNormalForceN[1] > 0.001f
          && droneDebug.contactNormalForceN[2] > 0.001f))
        return fail("Balanced Drone Bow did not excite both D and A strings");
    engine.stopBow();
    render(engine, left, right, 0.18);

    for (const auto sample : left)
        if (!std::isfinite(sample) || std::abs(sample) > 8.0f)
            return fail("Fiddle Play demo produced non-finite or runaway audio");

    const auto debug = engine.debugSnapshot();
    if (debug.bowPairLowerString != 1)
        return fail("Fiddle Play demo lost the D/A bow pair");

    if (std::abs(debug.speakingFrequencyHz[1] - 329.6276f) > 1.5f
        || std::abs(debug.speakingFrequencyHz[2] - 493.8833f) > 1.5f)
        return fail("Fiddle Play demo lost the held double-stop fingering");

    if (argc >= 2)
    {
        const std::filesystem::path output(argv[1]);
        if (!writeWav(output, left, right))
            return fail("Could not write Fiddle Play demo WAV");

        std::vector<float> gestureLeft;
        std::vector<float> gestureRight;
        const auto appendRange =
            [&](std::size_t begin, std::size_t end)
            {
                gestureLeft.insert(
                    gestureLeft.end(), left.begin() + begin, left.begin() + end);
                gestureRight.insert(
                    gestureRight.end(), right.begin() + begin, right.begin() + end);
                appendSilence(gestureLeft, gestureRight, 0.14);
            };

        appendRange(shortBegin, shortEnd);
        appendRange(accentBegin, accentEnd);
        appendRange(chopBegin, chopEnd);
        appendRange(tremoloBegin, tremoloEnd);
        appendRange(shuffleBegin, shuffleEnd);

        const auto gestureOutput =
            output.parent_path() / "11_fiddle_gesture_showcase.wav";
        if (!writeWav(gestureOutput, gestureLeft, gestureRight))
            return fail("Could not write Fiddle gesture showcase WAV");

        std::vector<float> focusLeft;
        std::vector<float> focusRight;
        focusLeft.insert(
            focusLeft.end(),
            left.begin() + singleFocusBegin,
            left.begin() + singleFocusEnd);
        focusRight.insert(
            focusRight.end(),
            right.begin() + singleFocusBegin,
            right.begin() + singleFocusEnd);
        appendSilence(focusLeft, focusRight, 0.18);
        focusLeft.insert(
            focusLeft.end(),
            left.begin() + droneBegin,
            left.begin() + droneEnd);
        focusRight.insert(
            focusRight.end(),
            right.begin() + droneBegin,
            right.begin() + droneEnd);

        const auto focusOutput =
            output.parent_path() / "16_single_focus_vs_drone.wav";
        if (!writeWav(focusOutput, focusLeft, focusRight))
            return fail("Could not write single-focus versus Drone Bow WAV");

        std::cout << "wav=" << output.string() << '\n'
                  << "gesture_wav=" << gestureOutput.string() << '\n'
                  << "focus_wav=" << focusOutput.string() << '\n';
    }

    std::cout << "PASS\n";
    return EXIT_SUCCESS;
}
