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


    // Musical continuity probe: the right hand holds one uninterrupted bow
    // while the left hand changes D-string positions. Pitch-only steady-state
    // regression cannot detect a disappearing bow, a transient amplitude hole,
    // a click at the note boundary, or an abrupt harmonic-colour change.
    // Keep colour metrics diagnostic until they can be calibrated against
    // matched real bowed-string transitions rather than flattening expressive
    // differences that may be entirely natural.
    {
        fiddle::FiddleEngine legato;
        legato.prepare(sampleRate);
        fiddle::Controls legatoControls;
        legatoControls.pressure = 0.56f;
        legatoControls.speed = 0.66f;
        legatoControls.attack = 0.78f;
        legatoControls.position = 0.48f;
        legatoControls.balance = -0.95f;
        legatoControls.singleStringIsolation = 1.0f;
        legato.setControls(legatoControls);

        constexpr std::array<float, 5> notes {
            293.6648f, 369.9944f, 440.0f, 369.9944f, 329.6276f
        };
        constexpr std::array<const char*, 5> noteNames {
            "D4", "Fsharp4", "A4", "Fsharp4_return", "E4"
        };
        constexpr double noteSeconds = 0.26;
        const auto segmentSamples =
            static_cast<std::size_t>(noteSeconds * sampleRate);
        const auto analysisSamples =
            static_cast<std::size_t>(0.060 * sampleRate);

        std::vector<float> legatoLeft;
        std::vector<float> legatoRight;
        legato.startBow(+1);
        for (std::size_t n = 0; n < notes.size(); ++n)
        {
            std::array<float, 4> fingers {};
            fingers[1] = notes[n];
            legato.setFingeringLayout(fingers, 1, 1, 0.88f);
            render(legato, legatoLeft, legatoRight, noteSeconds);
            if (legato.debugSnapshot().bowDirection != +1)
                return fail("Legato fingering interrupted the active bow direction");
        }
        legato.stopBow();
        render(legato, legatoLeft, legatoRight, 0.15);

        const auto rmsRange = [](const std::vector<float>& data,
                                 std::size_t from,
                                 std::size_t to)
        {
            from = std::min(from, data.size());
            to = std::min(to, data.size());
            double sum = 0.0;
            for (auto i = from; i < to; ++i)
                sum += static_cast<double>(data[i]) * data[i];
            return std::sqrt(sum / static_cast<double>(
                std::max<std::size_t>(1, to - from)));
        };
        const auto harmonicBalance = [](const std::vector<float>& data,
                                        std::size_t from,
                                        std::size_t to,
                                        double fundamental)
        {
            from = std::min(from, data.size());
            to = std::min(to, data.size());
            double low = 0.0, high = 0.0;
            for (int harmonic = 1; harmonic <= 8; ++harmonic)
            {
                const auto omega = 2.0 * 3.14159265358979323846
                    * fundamental * harmonic / sampleRate;
                double real = 0.0, imag = 0.0;
                for (auto i = from; i < to; ++i)
                {
                    const auto phase = omega * static_cast<double>(i - from);
                    const auto value = static_cast<double>(data[i]);
                    real += value * std::cos(phase);
                    imag += value * std::sin(phase);
                }
                const auto power = real * real + imag * imag;
                if (harmonic <= 3) low += power;
                else high += power;
            }
            return 10.0 * std::log10((high + 1.0e-20)
                                     / (low + 1.0e-20));
        };

        std::ofstream continuityCsv;
        if (argc >= 2)
        {
            const auto directory =
                std::filesystem::path(argv[1]).parent_path();
            if (!writeWav(directory / "19_continuous_bow_fingering.wav",
                          legatoLeft, legatoRight))
                return fail("Could not write continuous-bow fingering WAV");
            continuityCsv.open(directory / "legato_continuity_metrics.csv");
            if (!continuityCsv)
                return fail("Could not write legato continuity metrics CSV");
            continuityCsv
                << "from,to,boundary_seconds,pre_rms,transition_rms,"
                   "post_rms,transition_to_pre,post_to_pre,"
                   "pre_upper_harmonics_db,post_upper_harmonics_db,"
                   "boundary_jump_to_local_delta_rms\n";
        }

        for (std::size_t n = 1; n < notes.size(); ++n)
        {
            const auto boundary = n * segmentSamples;
            const auto preBegin = boundary - analysisSamples - 240;
            const auto preEnd = boundary - 240;
            const auto postBegin = boundary + 1920;
            const auto postEnd = postBegin + analysisSamples;
            const auto pre = rmsRange(legatoLeft, preBegin, preEnd);
            const auto transition = rmsRange(
                legatoLeft, boundary, boundary + 1440);
            const auto post = rmsRange(legatoLeft, postBegin, postEnd);
            const auto transitionRatio = transition / (pre + 1.0e-12);
            const auto postRatio = post / (pre + 1.0e-12);
            const auto preBalance = harmonicBalance(
                legatoLeft, preBegin, preEnd, notes[n - 1]);
            const auto postBalance = harmonicBalance(
                legatoLeft, postBegin, postEnd, notes[n]);

            double localDeltaPower = 0.0;
            const auto deltaBegin = boundary - 480;
            const auto deltaEnd = boundary + 480;
            for (auto i = deltaBegin; i < deltaEnd; ++i)
            {
                const auto diff = static_cast<double>(legatoLeft[i])
                    - legatoLeft[i - 1];
                localDeltaPower += diff * diff;
            }
            const auto deltaRms = std::sqrt(localDeltaPower
                / static_cast<double>(deltaEnd - deltaBegin));
            const auto boundaryJump = std::abs(
                static_cast<double>(legatoLeft[boundary])
                - legatoLeft[boundary - 1]) / (deltaRms + 1.0e-12);

            std::cout << "legato_transition " << noteNames[n - 1]
                      << "->" << noteNames[n]
                      << " transition_to_pre=" << transitionRatio
                      << " post_to_pre=" << postRatio
                      << " upper_harmonics_change_db="
                      << postBalance - preBalance
                      << " boundary_jump=" << boundaryJump << '\n';
            if (continuityCsv)
                continuityCsv << noteNames[n - 1] << ','
                              << noteNames[n] << ','
                              << static_cast<double>(boundary) / sampleRate
                              << ',' << pre << ',' << transition << ','
                              << post << ',' << transitionRatio << ','
                              << postRatio << ',' << preBalance << ','
                              << postBalance << ',' << boundaryJump << '\n';

            // Only guard catastrophic dropouts, runaways and a digital click.
            // Do NOT require similar timbre on different pitches: that
            // question needs reference recordings and a listening decision.
            if (!std::isfinite(transitionRatio)
                || !std::isfinite(postRatio)
                || !std::isfinite(postBalance)
                || pre < 1.0e-6
                || transitionRatio < 0.12
                || transitionRatio > 6.0
                || postRatio < 0.12
                || postRatio > 6.0
                || boundaryJump > 12.0)
                return fail("Continuous-bow fingering has an audible dropout, runaway or click");
        }
    }

    // Connected D -> A melodic string crossing. B4 is above the practical
    // stopped D position and needs the A string, but changing fingers should
    // not teleport the bow-pair reference from D/A to A/E while it is moving.
    {
        std::array<int, 4> gOnD { 67, -1, -1, -1 };
        const auto dLayout = fiddle::voiceFingering(
            gOnD, 1, 67, 1, 1);
        std::array<int, 4> bOnA { 71, -1, -1, -1 };
        const auto bLayout = fiddle::voiceFingering(
            bOnA, 1, 71, dLayout.primaryString,
            dLayout.bowPairLowerString);
        if (dLayout.primaryString != 1
            || bLayout.primaryString != 2
            || bLayout.bowPairLowerString != 1
            || fiddle::singleStringFocusForLayout(bLayout, 1) < 0.90f)
            return fail("Connected B4 fingering unexpectedly jumped the physical bow pair");
        // A new, unconnected B4 attack remains free to select A/E normally.
        const auto isolatedB = fiddle::voiceFingering(bOnA, 1, 71);
        if (isolatedB.bowPairLowerString != 2)
            return fail("New B4 stroke lost independent bow-pair selection");
    }

    // The uploaded musical reel plays repeated E5 notes after D5 on A.
    // Auto fingering can *legitimately* keep E5 as a fourth-finger A-string
    // note: hearing a "ping" at E5 does not prove the E string was bowed.
    // Confirm both routes and publish a matched audible comparison of the
    // same held-bow D5->E5 change on stopped A and open E.
    {
        std::array<int, 4> d5Note { 74, -1, -1, -1 };
        const auto d5Layout = fiddle::voiceFingering(
            d5Note, 1, 74, 2, 2);
        std::array<int, 4> e5Note { 76, -1, -1, -1 };
        const auto e5OnA = fiddle::voiceFingering(
            e5Note, 1, 76,
            d5Layout.primaryString, d5Layout.bowPairLowerString);
        std::array<int, 4> fSharp5Note { 78, -1, -1, -1 };
        const auto fSharpOnE = fiddle::voiceFingering(
            fSharp5Note, 1, 78,
            d5Layout.primaryString, d5Layout.bowPairLowerString);
        if (d5Layout.primaryString != 2
            || e5OnA.primaryString != 2
            || e5OnA.bowPairLowerString != 2
            || fSharpOnE.primaryString != 3
            || fSharpOnE.bowPairLowerString != 2)
            return fail("Musical A/E string assignment differs from E5 diagnostic");

        std::vector<float> eComparisonL, eComparisonR;
        std::ofstream eComparisonCsv;
        if (argc >= 2)
        {
            const auto path = std::filesystem::path(argv[1]).parent_path()
                / "e5_fingering_and_crossing_metrics.csv";
            eComparisonCsv.open(path);
            if (!eComparisonCsv)
                return fail("Cannot open E5 crossing metrics");
            eComparisonCsv
                << "variant,primary_string,pre_rms,first_35ms_rms,"
                   "later_rms,first_to_pre,later_to_pre,bow_direction\n";
        }

        const auto windowRms = [](const std::vector<float>& signal,
                                  std::size_t first, std::size_t count)
        {
            const auto end = std::min(signal.size(), first + count);
            double energy = 0.0;
            for (auto i = first; i < end; ++i)
            {
                const auto sample = static_cast<double>(signal[i]);
                energy += sample * sample;
            }
            return std::sqrt(energy
                / static_cast<double>(std::max<std::size_t>(1, end-first)));
        };

        for (int variant = 0; variant < 2; ++variant)
        {
            fiddle::FiddleEngine crossing;
            crossing.prepare(sampleRate);
            fiddle::Controls crossControls;
            crossControls.pressure = 0.56f;
            crossControls.speed = 0.66f;
            crossControls.attack = 0.78f;
            crossControls.position = 0.48f;
            crossControls.balance = -0.95f;
            crossControls.singleStringIsolation = 1.0f;
            crossing.setControls(crossControls);

            std::array<float, 4> pitch {};
            pitch[2] = 587.3295f; // stopped D5 on the physical A string
            crossing.setFingeringLayout(pitch, 2, 2, 0.85);
            crossing.startBow(+1);
            std::vector<float> segmentL, segmentR;
            render(crossing, segmentL, segmentR, 0.30);

            const auto boundary = segmentL.size();
            pitch.fill(0.0f);
            if (variant == 0)
                pitch[2] = 659.2551f; // fourth finger on A
            else
            {
                // Maintain bow direction but move the bow footprint onto E;
                // E5 on the E string is open (no fingered shortening).
                crossControls.balance = +0.95f;
                crossing.setControls(crossControls);
            }
            crossing.setFingeringLayout(
                pitch, variant == 0 ? 2 : 3, 2, 0.85);
            render(crossing, segmentL, segmentR, 0.36);
            const auto state = crossing.debugSnapshot();
            if (state.bowDirection != +1
                || state.primaryString != (variant == 0 ? 2 : 3))
                return fail("E5 crossing changed bow direction or physical string");

            const auto ms = [](double duration)
            {
                return static_cast<std::size_t>(
                    duration * sampleRate / 1000.0);
            };
            const auto before = windowRms(
                segmentL, boundary - ms(65), ms(55));
            const auto early = windowRms(segmentL, boundary, ms(35));
            const auto later = windowRms(
                segmentL, boundary + ms(70), ms(70));
            const auto firstRatio = early / (before + 1.0e-12);
            const auto laterRatio = later / (before + 1.0e-12);
            if (!std::isfinite(firstRatio)
                || !std::isfinite(laterRatio)
                || before < 1.0e-6)
                return fail("Invalid E5 physical crossing amplitude");

            const auto* name = variant == 0
                ? "E5_fourth_finger_A" : "E5_open_E_crossing";
            std::cout << "e5_transition " << name
                      << " pre_rms=" << before
                      << " first35_to_pre=" << firstRatio
                      << " later_to_pre=" << laterRatio
                      << " primary_string=" << state.primaryString
                      << '\n';
            if (eComparisonCsv)
                eComparisonCsv << name << ',' << state.primaryString << ','
                               << before << ',' << early << ',' << later
                               << ',' << firstRatio << ',' << laterRatio
                               << ',' << state.bowDirection << '\n';

            crossing.stopBow();
            render(crossing, segmentL, segmentR, 0.13);
            eComparisonL.insert(eComparisonL.end(),
                                segmentL.begin(), segmentL.end());
            eComparisonR.insert(eComparisonR.end(),
                                segmentR.begin(), segmentR.end());
            appendSilence(eComparisonL, eComparisonR, 0.20);
        }

        if (argc >= 2)
        {
            const auto out = std::filesystem::path(argv[1]).parent_path()
                / "20_E5_stopped_A_vs_open_E_crossing.wav";
            if (!writeWav(out, eComparisonL, eComparisonR))
                return fail("Could not write E5 comparison WAV");
        }
    }

    // Upper-A stopped note must not *directly bow* open E accidentally.
    // An unplayed E can still respond sympathetically at the shared bridge,
    // especially at the exact E5 unison, but there should be no extra
    // independent hair-driven voice when String Focus isolates A.
    {
        fiddle::FiddleEngine upperA;
        upperA.prepare(sampleRate);
        fiddle::Controls c;
        c.pressure = 0.56f;
        c.speed = 0.66f;
        c.attack = 0.78f;
        c.position = 0.48f;
        c.balance = -0.95f;
        c.singleStringIsolation = 1.0f;
        upperA.setControls(c);
        std::array<float, 4> stoppedA {};
        stoppedA[2] = 659.2551f;
        upperA.setFingeringLayout(stoppedA, 2, 2, 0.85);
        upperA.startBow(+1);
        std::vector<float> audioL, audioR;
        render(upperA, audioL, audioR, 0.25);
        auto state = upperA.debugSnapshot();
        if (!(state.contactNormalForceN[2] > 0.001f)
            || state.contactNormalForceN[3] > 1.0e-7f)
            return fail("Single A-string E5 still directly bows unplayed open E");

        // An intentional balanced A/E bow must retain both hair contacts.
        c.balance = 0.0f;
        c.singleStringIsolation = 0.0f;
        upperA.setControls(c);
        render(upperA, audioL, audioR, 0.30);
        state = upperA.debugSnapshot();
        if (!(state.contactNormalForceN[2] > 0.001f
              && state.contactNormalForceN[3] > 0.001f))
            return fail("A/E double-stop lost intentional E-string contact");
    }


    // Regression for the reported pitched "pon" at every A -> E string
    // crossing. The bow is still physically touching A on the very first
    // sample after MIDI moves the left hand to E. The contact-force transfer
    // must follow the smoothed bow angle, not the new string ID.
    {
        fiddle::FiddleEngine cross;
        cross.prepare(sampleRate);
        fiddle::Controls cc;
        cc.pressure = 0.56f;
        cc.speed = 0.66f;
        cc.attack = 0.78f;
        cc.position = 0.48f;
        cc.balance = -0.95f;
        cc.singleStringIsolation = 1.0f;
        cross.setControls(cc);
        std::array<float, 4> pitches {};
        pitches[2] = 493.8833f; // B4 on A
        cross.setFingeringLayout(pitches, 2, 2, 0.85f);
        cross.startBow(+1);

        std::vector<float> crossingLeft, crossingRight;
        render(cross, crossingLeft, crossingRight, 0.34);
        const auto beforeCross = cross.debugSnapshot();
        if (beforeCross.contactNormalForceN[2] < 0.001f)
            return fail("A/E transfer probe did not establish A-string bowing");

        pitches.fill(0.0f);
        pitches[3] = 739.9888f; // F#5, cannot be fingered on A in this mode
        cross.setFingeringLayout(pitches, 3, 2, 0.85f);
        cc.balance = +0.95f;
        cross.setControls(cc);

        std::ofstream transferCsv;
        if (argc >= 2)
        {
            transferCsv.open(std::filesystem::path(argv[1]).parent_path()
                / "a_to_e_bow_force_transfer.csv");
            if (!transferCsv)
                return fail("Cannot write A/E string crossing force trace");
            transferCsv << "time_ms,a_normal_force_n,e_normal_force_n,"
                           "a_sticking,e_sticking,bow_direction\n";
        }

        float firstA = 0.0f, firstE = 0.0f;
        double maxForceJump = 0.0;
        double lastA = beforeCross.contactNormalForceN[2];
        double lastE = beforeCross.contactNormalForceN[3];
        constexpr auto crossingSamples = static_cast<std::size_t>(
            0.080 * sampleRate);
        for (std::size_t k = 0; k < crossingSamples; ++k)
        {
            float l = 0.0f, r = 0.0f;
            cross.process(&l, &r, 1);
            crossingLeft.push_back(l);
            crossingRight.push_back(r);
            const auto state = cross.debugSnapshot();
            const auto aForce = static_cast<double>(
                state.contactNormalForceN[2]);
            const auto eForce = static_cast<double>(
                state.contactNormalForceN[3]);
            if (k == 0)
            {
                firstA = state.contactNormalForceN[2];
                firstE = state.contactNormalForceN[3];
            }
            maxForceJump = std::max(maxForceJump,
                std::max(std::abs(aForce-lastA), std::abs(eForce-lastE)));
            lastA = aForce;
            lastE = eForce;
            if (transferCsv && k % 24 == 0)
                transferCsv << (1000.0 * k / sampleRate)
                            << ',' << aForce << ',' << eForce
                            << ',' << (state.sticking[2] ? 1 : 0)
                            << ',' << (state.sticking[3] ? 1 : 0)
                            << ',' << state.bowDirection << '\n';
        }
        const auto afterCross = cross.debugSnapshot();
        std::cout << "a_to_e_transfer force_A_pre="
                  << beforeCross.contactNormalForceN[2]
                  << " force_E_first=" << firstE
                  << " force_A_first=" << firstA
                  << " force_E_post=" << afterCross.contactNormalForceN[3]
                  << " max_single_sample_force_jump=" << maxForceJump
                  << '\n';
        if (!(firstA > beforeCross.contactNormalForceN[2] * 0.70f)
            || !(firstE < beforeCross.contactNormalForceN[2] * 0.15f)
            || !(afterCross.contactNormalForceN[3] > 0.001f)
            || !(afterCross.contactNormalForceN[2]
                 < afterCross.contactNormalForceN[3] * 0.01f)
            || maxForceJump > 0.025
            || afterCross.bowDirection != +1)
            return fail("A to E crossing teleported bow force or failed to settle");
        cross.stopBow();
        render(cross, crossingLeft, crossingRight, 0.16);
        if (argc >= 2
            && !writeWav(
                std::filesystem::path(argv[1]).parent_path()
                    / "21_A_to_E_continuous_bow_crossing.wav",
                crossingLeft, crossingRight))
            return fail("Cannot write A to E bowed crossing WAV");
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
        || singleFocus.contactNormalForceN[2] > singlePairForce * 0.004f)
        return fail("Monophonic Fiddle Play directly bowed too much adjacent A string");
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
