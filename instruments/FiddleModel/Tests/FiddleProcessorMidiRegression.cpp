#include "PluginProcessor.h"
#include "Dsp/FiddlePlayLayout.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
// Use the actual audio-device rate for every pitch/spectral calculation.
// Standalone commonly runs at 44.1 kHz while the original regression only
// exercised 48 kHz, making otherwise identical gestures sound different.
double sampleRate = 48000.0;
constexpr int blockSize = 256;

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

double estimateDominantPitch(const std::vector<float>& x,
                             double lowHz,
                             double highHz)
{
    constexpr int candidates = 1400;
    const auto length = std::min<std::size_t>(
        x.size(), static_cast<std::size_t>(0.20 * sampleRate));
    const auto begin = x.size() - length;
    double bestFrequency = lowHz;
    double bestCorrelation = -2.0;

    for (int i = 0; i <= candidates; ++i)
    {
        const auto fraction = static_cast<double>(i) / candidates;
        const auto frequency = lowHz + (highHz - lowHz) * fraction;
        const auto corr = correlationAtFrequency(
            x, begin, x.size(), frequency);
        if (corr > bestCorrelation)
        {
            bestCorrelation = corr;
            bestFrequency = frequency;
        }
    }
    return bestFrequency;
}

double tonePower(const std::vector<float>& x, double frequency)
{
    const auto length = std::min<std::size_t>(
        x.size(), static_cast<std::size_t>(0.20 * sampleRate));
    const auto begin = x.size() - length;
    if (length < 256)
        return 0.0;

    double re = 0.0;
    double im = 0.0;
    for (std::size_t n = 0; n < length; ++n)
    {
        const auto phase =
            2.0 * 3.14159265358979323846 * frequency
            * static_cast<double>(n) / sampleRate;
        const auto window =
            0.5 - 0.5 * std::cos(
                2.0 * 3.14159265358979323846
                * static_cast<double>(n)
                / static_cast<double>(length - 1));
        const auto sample =
            static_cast<double>(x[begin + n]) * window;
        re += sample * std::cos(phase);
        im -= sample * std::sin(phase);
    }
    return re * re + im * im;
}

double harmonicCombPower(const std::vector<float>& x, double fundamental)
{
    double power = 0.0;
    for (int harmonic = 1; harmonic <= 8; ++harmonic)
    {
        const auto frequency = fundamental * harmonic;
        if (frequency >= 0.45 * sampleRate)
            break;
        power += tonePower(x, frequency)
            / std::sqrt(static_cast<double>(harmonic));
    }
    return power;
}

// Fifth-related G/D/A/E strings share real partials at multiples of
// 588 Hz. Do NOT stop comparing the other string at its eighth harmonic:
// D4 harmonic 6 (~1762 Hz) is G3 harmonic 9, and D4 harmonic 8 (~2349 Hz)
// is G3 harmonic 12. Both are excluded from the unshared evidence even
// though this metric measures only each candidate's first eight lines.
// The +8 dB *unshared* string-identity acceptance floor stays unchanged.
bool overlapsOtherStringHarmonic(double frequency,
                                 double otherFundamental)
{
    if (frequency <= 0.0 || otherFundamental <= 0.0)
        return false;

    const auto nearest = std::llround(frequency / otherFundamental);
    if (nearest < 1
        || nearest * otherFundamental >= 0.45 * sampleRate)
        return false;

    return std::abs(frequency - nearest * otherFundamental)
        <= std::max(2.0, 0.005 * frequency);
}

double unsharedHarmonicCombPower(const std::vector<float>& x,
                                 double fundamental,
                                 double otherFundamental)
{
    double power = 0.0;
    for (int harmonic = 1; harmonic <= 8; ++harmonic)
    {
        const auto frequency = fundamental * harmonic;
        if (frequency >= 0.45 * sampleRate)
            break;

        if (!overlapsOtherStringHarmonic(
                frequency, otherFundamental))
            power += tonePower(x, frequency)
                / std::sqrt(static_cast<double>(harmonic));
    }
    return power;
}

double centsBetween(double measured, double target)
{
    return 1200.0 * std::log2(measured / target);
}

float midiToHz(int note)
{
    return 440.0f * std::pow(
        2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

void renderBlocks(FiddleModelAudioProcessor& processor,
                  int blocks,
                  std::vector<float>& output,
                  const juce::MidiMessage* firstMessage = nullptr)
{
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;
        if (block == 0 && firstMessage != nullptr)
            midi.addEvent(*firstMessage, 0);

        processor.processBlock(buffer, midi);
        const auto* left = buffer.getReadPointer(0);
        output.insert(output.end(), left, left + blockSize);
    }
}

void appendBlock(const juce::AudioBuffer<float>& buffer,
                 std::vector<float>& output)
{
    const auto* left = buffer.getReadPointer(0);
    output.insert(output.end(), left, left + buffer.getNumSamples());
}

void writeU16(std::ofstream& out, std::uint16_t value)
{
    const char bytes[2] {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)
    };
    out.write(bytes, 2);
}

void writeU32(std::ofstream& out, std::uint32_t value)
{
    const char bytes[4] {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)
    };
    out.write(bytes, 4);
}

bool writeMonoWav(const std::filesystem::path& path,
                  const std::vector<float>& samples)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return false;

    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;

    float peak = 0.0f;
    for (const auto sample : samples)
        peak = std::max(peak, std::abs(sample));
    const auto gain = peak > 0.92f ? 0.92f / peak : 1.0f;

    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bits = 16;
    const auto frames = static_cast<std::uint32_t>(samples.size());
    const auto bytes = frames * channels * (bits / 8u);

    out.write("RIFF", 4); writeU32(out, 36u + bytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4); writeU32(out, 16u);
    writeU16(out, 1u); writeU16(out, channels);
    writeU32(out, static_cast<std::uint32_t>(sampleRate));
    writeU32(out, static_cast<std::uint32_t>(sampleRate) * 2u);
    writeU16(out, 2u); writeU16(out, bits);
    out.write("data", 4); writeU32(out, bytes);

    for (const auto sample : samples)
    {
        const auto clamped =
            std::clamp(sample * gain, -1.0f, 1.0f);
        const auto encoded = static_cast<std::int16_t>(
            std::lrint(clamped * 32767.0f));
        writeU16(out, static_cast<std::uint16_t>(encoded));
    }
    return static_cast<bool>(out);
}

// Long-form, real-processor stereo recordings for a HUMAN pitch-motion check.
// The original <0.3-second notes were too brief to audit the reported
// "stationary bowed foreground + faint moving pitch" by listening. These
// are NOT looped, transposed or oscillator-generated: every sample comes
// from a running FiddleModelAudioProcessor with one sustained physical bow.
struct StereoRecording
{
    std::vector<float> left;
    std::vector<float> right;
};

void renderStereoBlocks(FiddleModelAudioProcessor& processor,
                        int blocks, StereoRecording& output,
                        const juce::MidiMessage* firstNote = nullptr)
{
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;
        if (block == 0 && firstNote != nullptr)
            midi.addEvent(*firstNote, 0);
        processor.processBlock(buffer, midi);
        const auto* left = buffer.getReadPointer(0);
        const auto* right = buffer.getReadPointer(1);
        output.left.insert(output.left.end(), left, left + blockSize);
        output.right.insert(output.right.end(), right, right + blockSize);
    }
}

bool writeStereoWav(const std::filesystem::path& path,
                    const StereoRecording& output)
{
    if (output.left.size() != output.right.size())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return false;
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;

    float peak = 0.0f;
    for (std::size_t i = 0; i < output.left.size(); ++i)
        peak = std::max(peak,
            std::max(std::abs(output.left[i]), std::abs(output.right[i])));
    // Keep the processor's real two-channel level and relative note
    // dynamics. Only avoid actual PCM clipping if this recording peaks >.92.
    const auto gain = peak > 0.92f ? 0.92f / peak : 1.0f;
    const auto frames = static_cast<std::uint32_t>(output.left.size());
    const auto dataBytes = frames * 4u;
    out.write("RIFF", 4); writeU32(out, 36u + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4); writeU32(out, 16u);
    writeU16(out, 1u); writeU16(out, 2u);
    writeU32(out, static_cast<std::uint32_t>(sampleRate));
    writeU32(out, static_cast<std::uint32_t>(sampleRate) * 4u);
    writeU16(out, 4u); writeU16(out, 16u);
    out.write("data", 4); writeU32(out, dataBytes);
    for (std::size_t i = 0; i < output.left.size(); ++i)
    {
        for (const auto sample : { output.left[i], output.right[i] })
        {
            const auto y = static_cast<std::int16_t>(
                std::lrint(std::clamp(sample * gain, -1.0f, 1.0f) * 32767.0f));
            writeU16(out, static_cast<std::uint16_t>(y));
        }
    }
    return static_cast<bool>(out);
}

// Diagnostic for the player-reported "constant buzzer" timbre. Adjacent
// sample difference energy / sample energy measures the time-local high
// frequency content without an FFT, while the window-to-window coefficient
// of variation reveals unnaturally static spectra. It is an AUDIT, not a
// claim that a single numerical threshold defines a realistic violin.
struct SustainedTimbreAudit
{
    double brightnessMean = 0.0;
    double brightnessCv = 0.0;
    double rmsCv = 0.0;
    int validWindows = 0;
};

SustainedTimbreAudit measureSustainedTimbre(
    const std::vector<float>& playedSamples) noexcept
{
    constexpr double secondsPerWindow = 0.050;
    const auto windowLength = std::max<std::size_t>(
        16, static_cast<std::size_t>(secondsPerWindow * sampleRate));
    const auto skipOnset = static_cast<std::size_t>(0.30 * sampleRate);
    const auto skipRelease = static_cast<std::size_t>(0.10 * sampleRate);
    std::array<double, 2> sum {};
    std::array<double, 2> sumSq {};
    int count = 0;
    for (auto start = skipOnset;
         start + windowLength + skipRelease <= playedSamples.size();
         start += windowLength)
    {
        double energy = 0.0;
        double differenceEnergy = 0.0;
        auto prev = static_cast<double>(playedSamples[start]);
        for (std::size_t i = start; i < start + windowLength; ++i)
        {
            const auto y = static_cast<double>(playedSamples[i]);
            energy += y * y;
            const auto dy = y - prev;
            differenceEnergy += dy * dy;
            prev = y;
        }
        if (energy < 1.0e-16)
            continue;
        const auto rms = std::sqrt(energy / windowLength);
        const auto brightness = std::sqrt(
            differenceEnergy / (energy + 1.0e-30));
        const std::array<double, 2> values { brightness, rms };
        for (int i = 0; i < 2; ++i)
        {
            sum[static_cast<std::size_t>(i)] += values[static_cast<std::size_t>(i)];
            sumSq[static_cast<std::size_t>(i)] +=
                values[static_cast<std::size_t>(i)] *
                values[static_cast<std::size_t>(i)];
        }
        ++count;
    }

    SustainedTimbreAudit out;
    out.validWindows = count;
    if (count < 3)
        return out;
    const auto cv = [&](int i)
    {
        const auto idx = static_cast<std::size_t>(i);
        const auto mean = sum[idx] / count;
        const auto variance = std::max(0.0, sumSq[idx] / count - mean * mean);
        return std::sqrt(variance) / (mean + 1.0e-20);
    };
    out.brightnessMean = sum[0] / count;
    out.brightnessCv = cv(0);
    out.rmsCv = cv(1);
    return out;
}

bool renderPitchMotionAudition(const std::filesystem::path& outputDirectory,
                              int stringIndex,
                              bool gui,
                              std::ofstream& timeline)
{
    constexpr std::array<int, 4> openNotes { 55, 62, 69, 76 };
    constexpr std::array<const char*, 4> stringNames { "G", "D", "A", "E" };
    if (stringIndex < 0 || stringIndex >= static_cast<int>(openNotes.size()))
        return false;
    const int openNote = openNotes[static_cast<std::size_t>(stringIndex)];
    const auto* stringName = stringNames[static_cast<std::size_t>(stringIndex)];
    constexpr std::array<int, 5> intervals { 0, 5, 7, 2, 0 };
    constexpr int actionNote = 36; // C2 Down Bow
    constexpr float fingeringVelocity = 0.82f;
    FiddleModelAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);

    // Start from clean state, just as the built-in Reset Audition button.
    StereoRecording recording;
    StereoRecording discarded;
    const auto bow = juce::MidiMessage::noteOn(1, actionNote, 0.85f);
    if (gui)
    {
        renderStereoBlocks(processor, 4, discarded);
        processor.requestPlayActionFromUi(actionNote, true);
    }
    else
    {
        renderStereoBlocks(processor, 4, discarded, &bow);
    }

    const auto blocksPerNote = static_cast<int>(
        std::ceil(1.45 * sampleRate / static_cast<double>(blockSize)));
    bool passed = true;
    int previousNote = -1;
    for (std::size_t i = 0; i < intervals.size(); ++i)
    {
        const int note = openNote + intervals[i];
        const std::size_t firstFrame = recording.left.size();

        if (gui)
        {
            if (previousNote >= 0)
                processor.requestPlayFingeringFromUi(previousNote, false);
            processor.requestPlayFingeringFromUi(note, true);
            renderStereoBlocks(processor, blocksPerNote, recording);
        }
        else
        {
            // Keep C2 held across EVERY pitch change. This is a real note
            // switch, not pitch-shifting a sample or retriggering the bow.
            if (previousNote >= 0)
            {
                juce::AudioBuffer<float> transition(2, blockSize);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOff(
                    1, previousNote), 0);
                midi.addEvent(juce::MidiMessage::noteOn(
                    1, note, fingeringVelocity), 0);
                processor.processBlock(transition, midi);
                const auto* l = transition.getReadPointer(0);
                const auto* r = transition.getReadPointer(1);
                recording.left.insert(recording.left.end(),
                                      l, l + blockSize);
                recording.right.insert(recording.right.end(),
                                       r, r + blockSize);
                renderStereoBlocks(
                    processor, blocksPerNote - 1, recording);
            }
            else
            {
                const auto event = juce::MidiMessage::noteOn(
                    1, note, fingeringVelocity);
                renderStereoBlocks(processor, blocksPerNote,
                                   recording, &event);
            }
        }

        const auto state = processor.visualState();
        // Last 0.2s is a physically sustained tone; the earlier 0.28s clips
        // never let the listener hear settled bow motion.
        const std::vector<float> segment(
            recording.left.begin()
                + static_cast<std::ptrdiff_t>(firstFrame),
            recording.left.end());
        const auto target = static_cast<double>(midiToHz(note));
        const auto measured = estimateDominantPitch(
            segment,
            static_cast<double>(midiToHz(openNote)) * 0.94,
            static_cast<double>(midiToHz(openNote + 7)) * 1.04);
        const auto cents = centsBetween(measured, target);

        // The earlier 282ms pitch checks caught the fingering transition
        // but not the player's actual complaint: a bow-held G string
        // settled into a bright, nearly fixed scratch spectrum while the
        // moving pitched part became faint. Retain the established >=0.25
        // low-3 / first-16 harmonic power floor for EVERY sustained note,
        // including returns to the open string after a long held bow.
        double firstSixteenPower = 0.0;
        double lowThreePower = 0.0;
        for (int harmonic = 1; harmonic <= 16; ++harmonic)
        {
            if (target * harmonic >= 0.45 * sampleRate)
                break;
            const auto power = tonePower(segment, target * harmonic);
            firstSixteenPower += power;
            if (harmonic <= 3)
                lowThreePower += power;
        }
        const auto sustainedLowThreeFraction =
            lowThreePower / (firstSixteenPower + 1.0e-30);
        const auto timbre = measureSustainedTimbre(segment);

        const bool noteValid = state.midiNote == note
            && state.primaryString == stringIndex
            && std::isfinite(cents)
            && std::abs(cents) <= 10.0
            && sustainedLowThreeFraction >= 0.25;
        passed &= noteValid;

        const double beginning = firstFrame / sampleRate;
        const double ending = recording.left.size() / sampleRate;
        timeline << (gui ? "GUI" : "MIDI") << ','
                 << stringName << ','
                 << sampleRate << ',' << i << ',' << note << ','
                 << target << ',' << measured << ',' << cents << ','
                 << beginning << ',' << ending << ','
                 << state.primaryString << ','
                 << sustainedLowThreeFraction << ','
                 << timbre.brightnessMean << ','
                 << timbre.brightnessCv << ','
                 << timbre.rmsCv << ','
                 << (noteValid ? "PASS" : "FAIL") << '\n';
        std::cout << "processor_pitch_motion"
                  << " route=" << (gui ? "GUI" : "MIDI")
                  << " string=" << stringName
                  << " rate=" << sampleRate
                  << " note=" << note
                  << " duration=" << (ending - beginning)
                  << " target=" << target
                  << " measured=" << measured
                  << " cents=" << cents
                  << " low3_over16_fraction=" << sustainedLowThreeFraction
                  << " brightness_cv=" << timbre.brightnessCv
                  << " rms_cv=" << timbre.rmsCv
                  << " correct_string=" << (state.primaryString == stringIndex)
                  << '\n';
        previousNote = note;
    }

    if (gui)
    {
        processor.requestPlayFingeringFromUi(previousNote, false);
        processor.requestPlayActionFromUi(actionNote, false);
        renderStereoBlocks(
            processor, static_cast<int>(
                std::ceil(0.35 * sampleRate / blockSize)), recording);
    }
    else
    {
        juce::AudioBuffer<float> release(2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, previousNote), 0);
        midi.addEvent(juce::MidiMessage::noteOff(1, actionNote), 0);
        processor.processBlock(release, midi);
        const auto* l = release.getReadPointer(0);
        const auto* r = release.getReadPointer(1);
        recording.left.insert(recording.left.end(), l, l + blockSize);
        recording.right.insert(recording.right.end(), r, r + blockSize);
        renderStereoBlocks(
            processor, static_cast<int>(
                std::ceil(0.35 * sampleRate / blockSize)), recording);
    }

    const auto file = outputDirectory
        / (std::string("processor_pitch_motion_")
            + (gui ? "GUI_" : "MIDI_")
            + stringName + "_"
            + std::to_string(static_cast<int>(sampleRate)) + "hz.wav");
    if (!writeStereoWav(file, recording))
    {
        std::cerr << "FAIL: cannot export full pitch-motion reference WAV\n";
        return false;
    }
    if (!passed)
        std::cerr << "FAIL: long-form audible pitch-motion and physical string"
                  << " test route=" << (gui ? "GUI" : "MIDI")
                  << " string=" << stringIndex
                  << " rate=" << sampleRate << '\n';
    return passed;
}

// The direct engine onset probe can show excellent G/D fundamentals while
// the real MIDI Processor route still produces a weak first note. Sweep the
// *actual* C2-first Processor path (including JUCE parameter/gesture routing)
// so tuning is based on its measured sound, not on a simplified engine setup.
void printProcessorBowOnsetProbe(int stringIndex,
                                float pressure,
                                float speed,
                                float attack,
                                float position)
{
    FiddleModelAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);
    for (auto* parameter : processor.getParameters())
    {
        const auto name = parameter->getName(64);
        if (name == "Bow Pressure")
            parameter->setValueNotifyingHost(pressure);
        else if (name == "Bow Speed")
            parameter->setValueNotifyingHost(speed);
        else if (name == "Bow Response")
            parameter->setValueNotifyingHost(attack);
        else if (name == "Bow Contact")
            parameter->setValueNotifyingHost(position);
    }

    const auto downBow = juce::MidiMessage::noteOn(1, 36, 0.85f);
    std::vector<float> discard;
    renderBlocks(processor, 4, discard, &downBow);

    const int note = stringIndex == 0 ? 55 : 62;
    juce::AudioBuffer<float> eventBuffer(2, blockSize);
    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::noteOn(1, note, 0.82f), 96);
    processor.processBlock(eventBuffer, events);

    std::vector<float> segment;
    appendBlock(eventBuffer, segment);
    renderBlocks(processor, 52, segment);

    const auto target = static_cast<double>(midiToHz(note));
    const auto neighbour = static_cast<double>(midiToHz(note + 7));
    double firstEightPower = 0.0;
    double firstThreePower = 0.0;
    for (int harmonic = 1; harmonic <= 8; ++harmonic)
    {
        const auto power = tonePower(segment, target * harmonic);
        firstEightPower += power;
        if (harmonic <= 3)
            firstThreePower += power;
    }
    const auto fundamental =
        tonePower(segment, target) / (firstEightPower + 1.0e-30);
    const auto lowThree =
        firstThreePower / (firstEightPower + 1.0e-30);
    const auto unsharedAdvantage = 10.0 * std::log10(
        (unsharedHarmonicCombPower(segment, target, neighbour) + 1.0e-30)
        / (unsharedHarmonicCombPower(segment, neighbour, target) + 1.0e-30));

    std::cout << "processor_bow_onset_sweep"
              << " string=" << stringIndex
              << " pressure=" << pressure
              << " speed=" << speed
              << " attack=" << attack
              << " position=" << position
              << " fundamental_fraction=" << fundamental
              << " low3_fraction=" << lowThree
              << " target_vs_upper_unshared_db=" << unsharedAdvantage
              << '\n';
}

bool checkPitch(const std::vector<float>& segment,
                int note,
                int openNote,
                int previousNote,
                int stringIndex,
                int upperAdjacentOpenNote)
{
    const auto target = static_cast<double>(midiToHz(note));
    const auto openHz = static_cast<double>(midiToHz(openNote));
    const auto lowHz = openHz * 0.94;
    const auto highHz =
        static_cast<double>(midiToHz(openNote + 7)) * 1.04;
    const auto measured =
        estimateDominantPitch(segment, lowHz, highHz);
    const auto cents = centsBetween(measured, target);

    const auto targetPower =
        harmonicCombPower(segment, target);

    double firstEightPower = 0.0;
    double firstSixteenPower = 0.0;
    double lowThreePower = 0.0;
    for (int harmonic = 1; harmonic <= 16; ++harmonic)
    {
        if (target * harmonic >= 0.45 * sampleRate)
            break;
        const auto power = tonePower(segment, target * harmonic);
        firstSixteenPower += power;
        if (harmonic <= 8)
            firstEightPower += power;
        if (harmonic <= 3)
            lowThreePower += power;
    }
    const auto lowThreeFraction =
        lowThreePower / (firstEightPower + 1.0e-30);
    // The original eight-line denominator overlooks the deliberately strong
    // 9th-12th string harmonics around the 2 kHz bridge hill. In a GUI G3
    // onset, low3/8 can look healthy (>0.6) while low3/16 is <0.25 and the
    // player hears only a weak moving pitch behind a nearly fixed bright
    // bowed timbre. Measure both without dropping the old acceptance gate.
    const auto lowThreeWithinSixteenFraction =
        lowThreePower / (firstSixteenPower + 1.0e-30);

    double combAdvantageDb = 99.0;
    double previousCombAdvantageDb = 99.0;
    double adjacentCombAdvantageDb = 99.0;
    double adjacentUnsharedCombAdvantageDb = 99.0;
    if (note != openNote)
    {
        const auto staleOpenPower =
            harmonicCombPower(segment, openHz);
        combAdvantageDb = 10.0 * std::log10(
            (targetPower + 1.0e-30)
            / (staleOpenPower + 1.0e-30));
    }

    if (previousNote >= 0 && previousNote != note)
    {
        const auto previousHz =
            static_cast<double>(midiToHz(previousNote));
        const auto previousPower =
            harmonicCombPower(segment, previousHz);
        previousCombAdvantageDb = 10.0 * std::log10(
            (targetPower + 1.0e-30)
            / (previousPower + 1.0e-30));
    }

    if (upperAdjacentOpenNote >= 0)
    {
        const auto adjacentOpenHz =
            static_cast<double>(midiToHz(upperAdjacentOpenNote));
        const auto adjacentOpenPower =
            harmonicCombPower(segment, adjacentOpenHz);
        adjacentCombAdvantageDb = 10.0 * std::log10(
            (targetPower + 1.0e-30)
            / (adjacentOpenPower + 1.0e-30));

        const auto targetUnsharedPower =
            unsharedHarmonicCombPower(segment, target, adjacentOpenHz);
        const auto adjacentUnsharedPower =
            unsharedHarmonicCombPower(segment, adjacentOpenHz, target);
        adjacentUnsharedCombAdvantageDb = 10.0 * std::log10(
            (targetUnsharedPower + 1.0e-30)
            / (adjacentUnsharedPower + 1.0e-30));
    }

    std::cout << "processor_bow_first_pitch"
              << " string=" << stringIndex
              << " note=" << note
              << " target=" << target
              << " measured=" << measured
              << " cents=" << cents
              << " target_vs_open_comb_db=" << combAdvantageDb
              << " target_vs_previous_comb_db="
              << previousCombAdvantageDb
              << " target_vs_upper_adjacent_comb_db="
              << adjacentCombAdvantageDb
              << " target_vs_upper_adjacent_unshared_comb_db="
              << adjacentUnsharedCombAdvantageDb
              << " fundamental_fraction="
              << tonePower(segment, target) / (firstEightPower + 1.0e-30)
              << " low3_fraction=" << lowThreeFraction
              << " low3_over16_fraction=" << lowThreeWithinSixteenFraction
              << '\n';

    // Player-reported failure mode: low strings can contain the requested
    // pitch yet perceptually split into a fixed bowed/body sound plus a weak
    // pitch core. Guard both causes directly: the target comb must beat the
    // neighbouring open string even for an open primary string, and G/D must
    // keep meaningful energy in their first three note-locked harmonics.
    return std::abs(cents) <= 10.0
        && (note == openNote || combAdvantageDb >= 3.0)
        && (upperAdjacentOpenNote < 0
            || adjacentUnsharedCombAdvantageDb >= 8.0)
        && (stringIndex > 1
            || (lowThreeFraction >= 0.25
                && lowThreeWithinSixteenFraction >= 0.25));
}

bool runStringSequence(int stringIndex,
                       int openNote,
                       const std::filesystem::path* outputPath)
{
    FiddleModelAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);

    const auto downBow =
        juce::MidiMessage::noteOn(1, 36, 0.85f);
    std::vector<float> fullOutput;
    renderBlocks(processor, 4, fullOutput, &downBow);

    float armedPeak = 0.0f;
    for (const auto sample : fullOutput)
        armedPeak = std::max(armedPeak, std::abs(sample));
    if (armedPeak > 1.0e-5f)
    {
        std::cerr
            << "FAIL: bow-first action excited a default string before fingering"
            << " string=" << stringIndex
            << " peak=" << armedPeak << '\n';
        return false;
    }

    const int notes[4] {
        openNote,
        openNote + 1,
        openNote + 2,
        openNote + 6
    };

    const auto upperAdjacentOpenNote =
        stringIndex < 3 ? openNote + 7 : -1;

    bool allPitchesPassed = true;
    int previousNote = -1;
    for (int noteIndex = 0; noteIndex < 4; ++noteIndex)
    {
        const auto note = notes[noteIndex];
        const bool overlapPrevious = noteIndex >= 2;

        juce::AudioBuffer<float> eventBuffer(2, blockSize);
        juce::MidiBuffer events;

        if (overlapPrevious)
        {
            events.addEvent(
                juce::MidiMessage::noteOn(1, note, 0.82f), 0);
            if (previousNote >= 0)
                events.addEvent(
                    juce::MidiMessage::noteOff(1, previousNote), 32);
        }
        else
        {
            if (previousNote >= 0)
                events.addEvent(
                    juce::MidiMessage::noteOff(1, previousNote), 0);
            events.addEvent(
                juce::MidiMessage::noteOn(1, note, 0.82f), 96);
        }

        processor.processBlock(eventBuffer, events);
        appendBlock(eventBuffer, fullOutput);

        std::vector<float> segment;
        appendBlock(eventBuffer, segment);
        renderBlocks(processor, 52, segment);
        fullOutput.insert(
            fullOutput.end(),
            segment.begin() + blockSize,
            segment.end());

        const auto state = processor.visualState();
        if (state.primaryString != stringIndex)
        {
            std::cerr
                << "FAIL: bow-first phrase moved to wrong physical string"
                << " expected=" << stringIndex
                << " actual=" << state.primaryString << '\n';
            return false;
        }
        if (std::abs(state.stringFocus) < 0.85f)
        {
            std::cerr
                << "FAIL: bow-first fingering did not apply single-string focus"
                << " string=" << stringIndex
                << " focus=" << state.stringFocus << '\n';
            return false;
        }
        if (std::abs(
                state.speakingFrequencyHz[
                    static_cast<std::size_t>(stringIndex)]
                - midiToHz(note)) > 2.0f)
        {
            std::cerr
                << "FAIL: speaking frequency did not follow MIDI"
                << " string=" << stringIndex
                << " note=" << note
                << " speaking="
                << state.speakingFrequencyHz[
                    static_cast<std::size_t>(stringIndex)]
                << '\n';
            return false;
        }
        if (!checkPitch(
                segment,
                note,
                openNote,
                previousNote,
                stringIndex,
                upperAdjacentOpenNote))
        {
            // Preserve the actual failed Processor output, not an engine-only
            // approximation. The failure can happen before the full phrase is
            // rendered, so the usual success-only WAV export would discard
            // precisely the listening evidence we need.
            if (outputPath != nullptr)
            {
                const auto failurePath =
                    outputPath->parent_path()
                    / (std::string("processor_failed_")
                       + std::to_string(stringIndex)
                       + "_midi_" + std::to_string(note) + ".wav");
                if (!writeMonoWav(failurePath, fullOutput))
                    std::cerr << "FAIL: could not preserve failed Processor WAV\n";
                else
                    std::cerr << "diagnostic_failure_wav="
                              << failurePath.string() << '\n';
            }
            std::cerr
                << "FAIL: audible dominant pitch/energy did not follow fingering"
                << " string=" << stringIndex
                << " note=" << note << '\n';
            // Continue to collect all fingerings and all strings. Preserve
            // the original strict overall failure rather than allowing the
            // first bad open-string note to hide all later evidence.
            allPitchesPassed = false;
        }

        previousNote = note;
    }

    juce::AudioBuffer<float> releaseBuffer(2, blockSize);
    juce::MidiBuffer releaseMidi;
    releaseMidi.addEvent(
        juce::MidiMessage::noteOff(1, 36), 0);
    processor.processBlock(releaseBuffer, releaseMidi);
    appendBlock(releaseBuffer, fullOutput);

    if (outputPath != nullptr
        && !writeMonoWav(*outputPath, fullOutput))
    {
        std::cerr
            << "FAIL: could not write processor MIDI regression WAV\n";
        return false;
    }

    return allPitchesPassed;
}
// Audition through the exact requests sent by the GUI key map, rather than
// inferring Standalone behaviour from separate MIDI bow-first Note Ons.
bool runUiAuditionRegression(int stringIndex,
                            int openNote,
                            const std::filesystem::path* outputPath)
{
    FiddleModelAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);

    std::vector<float> silent;
    renderBlocks(processor, 4, silent);

    processor.requestPlayActionFromUi(36, true);
    processor.requestPlayFingeringFromUi(openNote, true);

    std::vector<float> opened;
    renderBlocks(processor, 53, opened);
    const auto firstState = processor.visualState();
    const auto firstOk = firstState.midiNote == openNote
        && firstState.primaryString == stringIndex
        && firstState.fingeringMask == fiddle::fingeringMaskBit(openNote);

    std::cout << "processor_ui_click"
              << " string=" << stringIndex
              << " note=" << openNote
              << " state_note=" << firstState.midiNote
              << " focus=" << firstState.stringFocus
              << " bow_action=" << firstState.bowAction
              << '\n';
    const auto openingPitchOk =
        checkPitch(opened, openNote, openNote, -1,
                   stringIndex, stringIndex < 3 ? openNote + 7 : -1);

    // A GUI drag may cross several narrow keys before a single audio block.
    // Replaying lossy press/release mailboxes used to leave old notes held.
    // Only the final key should be active, with no overlap or stale string.
    processor.requestPlayFingeringFromUi(openNote, false);
    processor.requestPlayFingeringFromUi(openNote + 1, true);
    processor.requestPlayFingeringFromUi(openNote + 1, false);
    processor.requestPlayFingeringFromUi(openNote + 2, true);

    std::vector<float> dragged;
    renderBlocks(processor, 53, dragged);
    const auto dragState = processor.visualState();
    const auto dragOk = dragState.midiNote == openNote + 2
        && dragState.primaryString == stringIndex
        && dragState.fingeringMask
            == fiddle::fingeringMaskBit(openNote + 2);

    std::cout << "processor_ui_drag"
              << " string=" << stringIndex
              << " note=" << openNote + 2
              << " state_note=" << dragState.midiNote
              << " mask=" << dragState.fingeringMask
              << " focus=" << dragState.stringFocus
              << '\n';
    const auto dragPitchOk = checkPitch(
        dragged, openNote + 2, openNote, openNote,
        stringIndex, stringIndex < 3 ? openNote + 7 : -1);

    processor.requestPlayFingeringFromUi(openNote + 2, false);
    processor.requestPlayActionFromUi(36, false);
    std::vector<float> released;
    renderBlocks(processor, 4, released);
    const auto releasedState = processor.visualState();
    const auto releaseOk =
        releasedState.midiNote == -1 && releasedState.fingeringMask == 0;

    if (outputPath != nullptr)
    {
        std::vector<float> full;
        full.reserve(opened.size() + dragged.size() + released.size());
        full.insert(full.end(), opened.begin(), opened.end());
        full.insert(full.end(), dragged.begin(), dragged.end());
        full.insert(full.end(), released.begin(), released.end());
        if (!writeMonoWav(*outputPath, full))
        {
            std::cerr << "FAIL: cannot write GUI audition WAV\n";
            return false;
        }
    }

    if (!(firstOk && openingPitchOk && dragOk
          && dragPitchOk && releaseOk))
    {
        std::cerr << "FAIL: GUI mouse click/drag audible fingering divergence"
                  << " string=" << stringIndex
                  << " initial_state=" << firstOk
                  << " initial_pitch=" << openingPitchOk
                  << " dragged_state=" << dragOk
                  << " dragged_pitch=" << dragPitchOk
                  << " release=" << releaseOk
                  << '\n';
        return false;
    }
    return true;
}

// Diagnostic-only physical control sweep through the *exact* Standalone
// key-map callback path. Earlier controls were optimized for MIDI bow-first
// and the strong G9..G12 bridge-hill partials in GUI audition were missed by
// the old eight-line score. Sweep controller gestures without weakening or
// changing any pass/fail threshold, then choose a stable physical region for
// both click and legato drag at 44.1 and 48 kHz.
void printGuiBowContactSweep(int stringIndex,
                             float pressure, float speed, float position)
{
    FiddleModelAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);
    for (auto* parameter : processor.getParameters())
    {
        const auto name = parameter->getName(64);
        if (name == "Bow Pressure")
            parameter->setValueNotifyingHost(pressure);
        else if (name == "Bow Speed")
            parameter->setValueNotifyingHost(speed);
        else if (name == "Bow Contact")
            parameter->setValueNotifyingHost(position);
    }

    const int note = stringIndex == 0 ? 55 : 62;
    std::vector<float> initialSilence;
    renderBlocks(processor, 4, initialSilence);
    processor.requestPlayActionFromUi(36, true);
    processor.requestPlayFingeringFromUi(note, true);

    const auto print = [&](int soundingNote, const char* phase,
                           const std::vector<float>& segment)
    {
        const auto hz = static_cast<double>(midiToHz(soundingNote));
        const auto neighbourHz =
            static_cast<double>(midiToHz(note + 7));
        double low3 = 0.0;
        double first8 = 0.0;
        double first16 = 0.0;
        for (int partial = 1; partial <= 16; ++partial)
        {
            if (partial * hz >= 0.45 * sampleRate)
                break;
            const auto energy = tonePower(segment, partial * hz);
            first16 += energy;
            if (partial <= 8)
                first8 += energy;
            if (partial <= 3)
                low3 += energy;
        }
        const auto targetUnshared =
            unsharedHarmonicCombPower(segment, hz, neighbourHz);
        const auto neighbourUnshared =
            unsharedHarmonicCombPower(segment, neighbourHz, hz);
        const auto margin = 10.0 * std::log10(
            (targetUnshared + 1.0e-30)
            / (neighbourUnshared + 1.0e-30));
        std::cout << "processor_gui_physics_sweep"
                  << " sample_rate=" << sampleRate
                  << " string=" << stringIndex
                  << " phase=" << phase
                  << " note=" << soundingNote
                  << " pressure=" << pressure
                  << " speed=" << speed
                  << " position=" << position
                  << " low3_over8_fraction="
                  << low3 / (first8 + 1.0e-30)
                  << " low3_over16_fraction="
                  << low3 / (first16 + 1.0e-30)
                  << " unshared_db=" << margin
                  << '\n';
    };

    std::vector<float> first;
    renderBlocks(processor, 53, first);
    print(note, "click", first);

    processor.requestPlayFingeringFromUi(note, false);
    processor.requestPlayFingeringFromUi(note + 1, true);
    processor.requestPlayFingeringFromUi(note + 1, false);
    processor.requestPlayFingeringFromUi(note + 2, true);

    std::vector<float> next;
    renderBlocks(processor, 53, next);
    print(note + 2, "drag", next);
}

} // namespace

// Long-form musical validation sequence mirrored by
// fiddle_play_validation_reel.mid. This is intentionally a musical workflow
// test, not another isolated oscillator/pitch probe: fingering and bow actions
// are scheduled exactly like a DAW performance and rendered through the real
// processor. The audio remains a human listening artifact; state assertions
// below only verify that the intended performance grammar survives MIDI routing.
bool renderFiddleValidationReel(const std::filesystem::path& outputDirectory)
{
    constexpr int ppq = 480;
    constexpr int bpm = 120;
    constexpr int barTicks = ppq * 4;
    constexpr int quarter = ppq;
    constexpr int eighth = ppq / 2;
    constexpr int down = 36;
    constexpr int shuffle = 37;
    constexpr int up = 38;
    constexpr int shortStroke = 40;
    constexpr int tremolo = 41;
    constexpr int drone = 43;
    constexpr int accent = 45;
    constexpr int chop = 46;
    constexpr int release = 47;

    struct ScheduledEvent
    {
        int tick = 0;
        juce::MidiMessage message;
    };

    std::vector<ScheduledEvent> events;
    const auto add = [&](int tick, const juce::MidiMessage& message)
    {
        events.push_back({ tick, message });
    };
    const auto addNote = [&](int startTick, int endTick, int note, int velocity)
    {
        add(startTick, juce::MidiMessage::noteOn(
            1, note, static_cast<float>(velocity) / 127.0f));
        add(endTick, juce::MidiMessage::noteOff(1, note));
    };
    const auto addPressure = [&](int tick, int value)
    {
        add(tick, juce::MidiMessage::channelPressureChange(1, value));
    };
    const auto addCc = [&](int tick, int controller, int value)
    {
        add(tick, juce::MidiMessage::controllerEvent(
            1, controller, value));
    };
    const auto addBend = [&](int tick, int value)
    {
        add(tick, juce::MidiMessage::pitchWheel(
            1, std::clamp(value, 0, 16383)));
    };

    const auto addSlurBar = [&](int bar,
                                int action,
                                int actionVelocity,
                                std::array<int, 4> notes,
                                int pressure,
                                int vibrato)
    {
        const auto base = (bar - 1) * barTicks;
        for (int i = 0; i < 4; ++i)
        {
            const auto start = i == 0
                ? base + i * quarter
                : base + i * quarter - 12;
            const auto end = i < 3
                ? base + (i + 1) * quarter + 12
                : base + barTicks - 24;
            addNote(start, end, notes[static_cast<std::size_t>(i)], 100);
        }
        // Adjacent sustained bow directions overlap by 24 ticks. The new
        // action becomes active before the old Note Off arrives, so the engine
        // performs a connected physical bow reversal instead of inserting an
        // artificial silence between Down/Up strokes.
        const auto bowStart = bar == 1 ? base + 12 : base - 12;
        const auto bowEnd = bar < 4
            ? base + barTicks + 12
            : base + barTicks - 20;
        addNote(bowStart, bowEnd, action, actionVelocity);
        addPressure(bowStart, pressure);
        if (vibrato >= 0)
            addCc(base + 2 * quarter, 1, vibrato);
    };

    addSlurBar(1, down, 86, { 62, 66, 69, 66 }, 48, -1);
    addSlurBar(2, up,   82, { 64, 67, 71, 69 }, 44, -1);
    addSlurBar(3, down, 90, { 66, 69, 74, 73 }, 55, 24);
    addSlurBar(4, up,   84, { 71, 69, 66, 64 }, 46, -1);

    constexpr std::array<std::array<int, 8>, 2> shortPhrases {{
        {{ 62, 66, 69, 71, 69, 66, 64, 62 }},
        {{ 64, 67, 71, 73, 71, 67, 66, 64 }}
    }};
    for (int phrase = 0; phrase < 2; ++phrase)
    {
        const auto base = (4 + phrase) * barTicks;
        for (int i = 0; i < 8; ++i)
        {
            const auto grid = base + i * eighth;
            addNote(grid, grid + eighth - 24,
                    shortPhrases[static_cast<std::size_t>(phrase)]
                                [static_cast<std::size_t>(i)],
                    98);
            addNote(grid + 10, grid + 82, shortStroke, 92);
        }
        addPressure(base, phrase == 0 ? 50 : 58);
    }

    {
        const auto base = 6 * barTicks;
        constexpr std::array<int, 4> notes { 62, 69, 66, 69 };
        for (int i = 0; i < 4; ++i)
        {
            const auto grid = base + i * quarter;
            addNote(grid, grid + quarter - 36,
                    notes[static_cast<std::size_t>(i)], 102);
            addNote(grid + 10, grid + 92, accent, 112);
        }
        addPressure(base, 64);
    }

    {
        const auto base = 7 * barTicks;
        addNote(base, base + barTicks - 24, 62, 100);
        addNote(base, base + barTicks - 24, 69, 100);
        for (const auto offset : { quarter, 2 * quarter, 3 * quarter })
            addNote(base + offset, base + offset + 72, chop, 104);
    }

    {
        const auto base = 8 * barTicks;
        constexpr std::array<int, 16> notes {
            62, 64, 66, 69, 66, 64, 62, 69,
            71, 69, 66, 64, 62, 64, 66, 69
        };
        for (int i = 0; i < static_cast<int>(notes.size()); ++i)
        {
            const auto grid = base + i * eighth;
            const auto start = i == 0 ? grid : grid - 10;
            const auto end = i + 1 < static_cast<int>(notes.size())
                ? grid + eighth + 10
                : base + 2 * barTicks - 30;
            addNote(start, end, notes[static_cast<std::size_t>(i)], 100);
        }
        addNote(base + 12, base + 2 * barTicks - 36, shuffle, 70);
        addPressure(base, 56);
        addNote(base + 2 * barTicks - 30,
                base + 2 * barTicks - 5, release, 64);
    }

    {
        const auto base = 10 * barTicks;
        addNote(base, base + barTicks - 24, 62, 100);
        addNote(base + 12, base + barTicks - 36, drone, 80);
        addPressure(base, 50);
    }

    {
        const auto base = 11 * barTicks;
        addCc(base, 64, 127);
        addNote(base + 12, base + 180, 64, 100);
        addNote(base + 12, base + 180, 71, 100);
        addNote(base + 220, base + barTicks - 180, down, 84);
        addCc(base + barTicks - 120, 64, 0);
    }

    {
        const auto base = 12 * barTicks;
        addNote(base, base + barTicks - 36, 73, 100);
        addNote(base + 12, base + barTicks - 30, up, 80);
        addCc(base + 60, 1, 46);
        addBend(base, 8192);
        for (int i = 1; i <= 8; ++i)
            addBend(
                base + quarter + i * quarter / 8,
                8192 + 4095 * i / 8);
        for (int i = 1; i <= 8; ++i)
            addBend(
                base + 3 * quarter + i * (quarter - 60) / 8,
                12287 - 4095 * i / 8);
        addBend(base + barTicks - 24, 8192);
        addCc(base + barTicks - 24, 1, 0);
    }

    {
        const auto base = 13 * barTicks;
        addNote(base, base + barTicks - 24, 74, 100);
        addNote(base + 12, base + barTicks - 36, tremolo, 78);
        addCc(base + quarter, 1, 36);
        addPressure(base + 2 * quarter, 60);
        addCc(base + barTicks - 24, 1, 0);
    }

    {
        const auto base = 14 * barTicks;
        constexpr std::array<int, 8> notes {
            69, 71, 73, 74, 76, 78, 76, 74
        };
        for (int i = 0; i < static_cast<int>(notes.size()); ++i)
        {
            const auto grid = base + i * eighth;
            const auto start = i == 0 ? grid : grid - 10;
            const auto end = i + 1 < static_cast<int>(notes.size())
                ? grid + eighth + 10
                : base + barTicks - 30;
            addNote(start, end, notes[static_cast<std::size_t>(i)], 100);
        }
        addNote(base + 12, base + barTicks - 36, down, 86);
        addPressure(base, 52);
    }

    {
        const auto base = 15 * barTicks;
        addNote(base, base + barTicks - 120, 74, 100);
        addNote(base, base + barTicks - 120, 78, 100);
        addNote(base + 30, base + 120, accent, 118);
        addNote(base + 3 * quarter,
                base + 3 * quarter + 36, release, 64);
        addPressure(base, 68);
    }

    const auto endTick = 16 * barTicks;
    addCc(endTick - 12, 1, 0);
    addCc(endTick - 12, 64, 0);
    addBend(endTick - 12, 8192);
    addPressure(endTick - 12, 0);

    std::stable_sort(
        events.begin(), events.end(),
        [](const ScheduledEvent& a, const ScheduledEvent& b)
        {
            return a.tick < b.tick;
        });

    FiddleModelAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);
    for (auto* parameter : processor.getParameters())
        if (parameter->getName(64) == "Play Mode")
            parameter->setValueNotifyingHost(1.0f);

    const auto samplesPerTick =
        sampleRate * 60.0 / (static_cast<double>(bpm) * ppq);
    const auto totalSamples = static_cast<std::int64_t>(
        std::ceil((endTick * samplesPerTick) + sampleRate));

    StereoRecording recording;
    recording.left.reserve(static_cast<std::size_t>(totalSamples));
    recording.right.reserve(static_cast<std::size_t>(totalSamples));

    const auto tickToSample = [&](int tick)
    {
        return static_cast<std::int64_t>(
            std::llround(tick * samplesPerTick));
    };

    struct Checkpoint
    {
        int tick;
        const char* name;
    };
    constexpr Checkpoint checkpoints[] {
        { 8 * barTicks + 2 * quarter, "shuffle" },
        { 11 * barTicks + 300, "held_double_stop" },
        { 12 * barTicks + 2 * quarter, "pitch_bend_slide" },
        { 13 * barTicks + 2 * quarter, "tremolo" },
        { 14 * barTicks + 4 * eighth + 80, "a_string_before_crossing" },
        { 14 * barTicks + 5 * eighth + 80, "e_string_crossing" },
        { 14 * barTicks + 7 * eighth + 80, "a_string_return" }
    };
    std::array<bool, std::size(checkpoints)> checkpointWritten {};
    std::ofstream checkpointCsv(
        outputDirectory / "processor_fiddle_validation_checkpoints.csv");
    if (!checkpointCsv)
        return false;
    checkpointCsv
        << "checkpoint,tick,seconds,active_note,primary_string,pair_lower,"
           "bow_action,fingering_hold,fingering_mask,speaking_g,speaking_d,"
           "speaking_a,speaking_e\n";

    std::size_t eventIndex = 0;
    bool finite = true;
    bool semanticsPassed = true;
    double peak = 0.0;

    for (std::int64_t blockStart = 0;
         blockStart < totalSamples;
         blockStart += blockSize)
    {
        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;
        const auto blockEnd = blockStart + blockSize;

        while (eventIndex < events.size())
        {
            const auto eventSample =
                tickToSample(events[eventIndex].tick);
            if (eventSample >= blockEnd)
                break;
            if (eventSample >= blockStart)
                midi.addEvent(
                    events[eventIndex].message,
                    static_cast<int>(eventSample - blockStart));
            ++eventIndex;
        }

        processor.processBlock(buffer, midi);
        const auto* left = buffer.getReadPointer(0);
        const auto* right = buffer.getReadPointer(1);
        recording.left.insert(
            recording.left.end(), left, left + blockSize);
        recording.right.insert(
            recording.right.end(), right, right + blockSize);

        for (int sample = 0; sample < blockSize; ++sample)
        {
            finite = finite
                && std::isfinite(left[sample])
                && std::isfinite(right[sample]);
            peak = std::max(
                peak,
                std::max(
                    std::abs(static_cast<double>(left[sample])),
                    std::abs(static_cast<double>(right[sample]))));
        }

        for (std::size_t i = 0; i < std::size(checkpoints); ++i)
        {
            if (checkpointWritten[i])
                continue;
            const auto checkpointSample =
                tickToSample(checkpoints[i].tick);
            if (checkpointSample >= blockStart
                && checkpointSample < blockEnd)
            {
                const auto state = processor.visualState();
                checkpointCsv
                    << checkpoints[i].name << ','
                    << checkpoints[i].tick << ','
                    << checkpointSample / sampleRate << ','
                    << state.midiNote << ','
                    << state.primaryString << ','
                    << state.pairLowerString << ','
                    << state.bowAction << ','
                    << (state.fingeringHold ? 1 : 0) << ','
                    << state.fingeringMask;
                for (const auto frequency : state.speakingFrequencyHz)
                    checkpointCsv << ',' << frequency;
                checkpointCsv << '\n';

                // These are semantic routing checks only. They deliberately do
                // not impose timbre/pitch-quality thresholds on the music.
                if (i == 0)
                    semanticsPassed = semanticsPassed
                        && state.bowAction
                            == static_cast<int>(fiddle::BowAction::Shuffle);
                else if (i == 1)
                {
                    const auto expectedMask =
                        fiddle::fingeringMaskBit(64)
                        | fiddle::fingeringMaskBit(71);
                    semanticsPassed = semanticsPassed
                        && state.fingeringHold
                        && (state.fingeringMask & expectedMask) == expectedMask
                        && state.primaryString == 2
                        && state.pairLowerString == 1;
                }
                else if (i == 2)
                    semanticsPassed = semanticsPassed
                        && state.midiNote == 73
                        && state.primaryString == 2
                        && state.speakingFrequencyHz[2] > 570.0f;
                else if (i == 3)
                    semanticsPassed = semanticsPassed
                        && state.bowAction
                            == static_cast<int>(fiddle::BowAction::Tremolo)
                        && state.primaryString == 2;
                else if (i == 4)
                    semanticsPassed = semanticsPassed
                        && state.primaryString == 2;
                else if (i == 5)
                    semanticsPassed = semanticsPassed
                        && state.midiNote == 78
                        && state.primaryString == 3;
                else if (i == 6)
                    semanticsPassed = semanticsPassed
                        && state.midiNote == 74
                        && state.primaryString == 2;

                checkpointWritten[i] = true;
            }
        }
    }

    if (!finite || peak <= 1.0e-5 || peak > 1.5)
    {
        std::cerr
            << "FAIL: validation reel audio unsafe/nonfinite peak="
            << peak << '\n';
        return false;
    }
    if (!semanticsPassed)
    {
        std::cerr
            << "FAIL: validation reel did not follow intended bow/fingering semantics\n";
        return false;
    }

    const auto allCheckpoints =
        std::all_of(
            checkpointWritten.begin(),
            checkpointWritten.end(),
            [](bool written) { return written; });
    if (!allCheckpoints)
    {
        std::cerr << "FAIL: validation reel missed state checkpoint\n";
        return false;
    }

    if (!writeStereoWav(
            outputDirectory / "processor_fiddle_validation_reel.wav",
            recording))
        return false;

    std::ofstream readme(
        outputDirectory / "READ_ME_fiddle_validation_reel.txt");
    readme
        << "FIDDLE PLAY VALIDATION REEL / 120 BPM / 16 bars\n"
        << "Mirrors the downloadable fiddle_play_validation_reel.mid.\n"
        << "Bars 1-4: sustained Down/Up bow slurs.\n"
        << "Bars 5-6: Short Stroke eighth notes.\n"
        << "Bar 7: Accent Stroke.  Bar 8: Chop double-stop test.\n"
        << "Bars 9-10: Nashville Shuffle.\n"
        << "Bar 11: Drone Bow.  Bar 12: CC64-held E4+B4 double stop.\n"
        << "Bar 13: C#5->D5 Pitch Bend slide on the selected string.\n"
        << "Bar 14: Tremolo.  Bar 15: A/E string crossing.\n"
        << "Bar 16: final D5+F#5 accented double stop.\n"
        << "This WAV is a listening audit, not proof of realism.\n";
    if (!readme)
        return false;

    std::cout
        << "processor_fiddle_validation_reel peak=" << peak
        << " events=" << events.size()
        << " seconds=" << totalSamples / sampleRate
        << '\n';
    return true;
}

int main(int argc, char** argv)
{
    // Guard the G/D harmonic-aliasing trap that produced false "open D"
    // failures in the actual GUI WAV while the 9th/12th G partials were
    // strongly radiating near 1.76/2.35 kHz.
    const auto g3 = static_cast<double>(midiToHz(55));
    const auto d4 = static_cast<double>(midiToHz(62));
    if (!overlapsOtherStringHarmonic(8.0 * d4, g3)
        || !overlapsOtherStringHarmonic(6.0 * d4, g3)
        || overlapsOtherStringHarmonic(7.0 * d4, g3))
    {
        std::cerr << "FAIL: adjacent-string harmonic ownership regression\n";
        return EXIT_FAILURE;
    }

    const std::filesystem::path outputDirectory =
        argc >= 2
            ? std::filesystem::path(argv[1])
            : std::filesystem::path {};

    struct StringCase
    {
        int index;
        int openNote;
        const char* name;
    };

    constexpr StringCase cases[] {
        { 0, 55, "G" },
        { 1, 62, "D" },
        { 2, 69, "A" },
        { 3, 76, "E" }
    };

    bool allStringsPassed = true;
    // Diagnostic-only (no pass/fail threshold changes): identify parameter
    // ranges where the real Processor begins with a strong pitched low string.
    // One parameter varies at a time so physical causes stay interpretable.
    constexpr float probeControls[][4] {
        { 0.50f, 0.50f, 0.50f, 0.45f },
        { 0.50f, 0.50f, 0.35f, 0.45f },
        { 0.50f, 0.50f, 0.65f, 0.45f },
        { 0.50f, 0.50f, 0.80f, 0.45f },
        { 0.50f, 0.35f, 0.50f, 0.45f },
        { 0.50f, 0.40f, 0.50f, 0.45f },
        { 0.50f, 0.60f, 0.50f, 0.45f },
        { 0.50f, 0.75f, 0.50f, 0.45f },
        { 0.35f, 0.50f, 0.50f, 0.45f },
        { 0.65f, 0.50f, 0.50f, 0.45f },
        { 0.80f, 0.50f, 0.50f, 0.45f },
        { 0.50f, 0.50f, 0.50f, 0.30f },
        { 0.50f, 0.50f, 0.50f, 0.35f },
        { 0.50f, 0.50f, 0.50f, 0.55f }
    };
    for (int lowString = 0; lowString <= 1; ++lowString)
        for (const auto& c : probeControls)
            printProcessorBowOnsetProbe(
                lowString, c[0], c[1], c[2], c[3]);

    for (const auto& item : cases)
    {
        std::filesystem::path outputPath;
        const std::filesystem::path* outputPtr = nullptr;
        if (!outputDirectory.empty())
        {
            outputPath = outputDirectory
                / (std::string("processor_bow_first_")
                   + item.name + ".wav");
            outputPtr = &outputPath;
        }

        if (!runStringSequence(
                item.index, item.openNote, outputPtr))
            allStringsPassed = false;
    }

    // Still run the real GUI path when a MIDI case fails: we must collect
    // all independent acoustical evidence in one costly Windows build rather
    // than hiding the GUI diagnostic behind an unrelated early failure.
    // Verify the Standalone's Play Key Map shortcut separately. It presses
    // Down Bow and the first fingering in the *same* processBlock, unlike the
    // earlier MIDI-only bow-first test, and can traverse multiple notes before
    // a single callback.
    bool uiPassed = true;
    for (const auto rate : { 48000.0, 44100.0 })
    {
        sampleRate = rate;
        for (const auto& item : cases)
        {
            std::filesystem::path outputPath;
            const std::filesystem::path* outputPtr = nullptr;
            if (!outputDirectory.empty())
            {
                outputPath = outputDirectory
                    / (std::string("processor_ui_click_drag_")
                       + item.name + "_"
                       + std::to_string(static_cast<int>(rate))
                       + "hz.wav");
                outputPtr = &outputPath;
            }
            std::cout << "processor_ui_sample_rate=" << rate
                      << " string=" << item.index << '\n';
            if (!runUiAuditionRegression(
                    item.index, item.openNote, outputPtr))
                uiPassed = false;
        }
    }

    // Produce genuine long-form pitch movement for ALL FOUR physical strings
    // and BOTH real plugin input paths. Every phrase starts on the open string
    // and moves to stopped pitches while retaining a continuous down bow.
    // Give the listener a direct root-vs-fingered comparison rather than only
    // asserting numerically that the target pitch was detected.
    const auto motionDirectory = outputDirectory.empty()
        ? std::filesystem::path { "." }
        : outputDirectory;
    std::error_code motionDirectoryError;
    std::filesystem::create_directories(
        motionDirectory, motionDirectoryError);
    bool motionPassed = !motionDirectoryError;
    std::ofstream timeline(
        motionDirectory / "processor_pitch_motion_timeline.csv");
    if (!timeline)
        motionPassed = false;
    timeline << "route,string,sample_rate,step,midi_note,target_hz,"
                "estimated_hz,cents,start_seconds,end_seconds,"
                "physical_string,low3_over16_fraction,"
                "brightness_proxy,brightness_cv,rms_cv,validation\n";
    for (const auto rate : { 48000.0, 44100.0 })
    {
        sampleRate = rate;
        for (const auto stringIndex : { 0, 1, 2, 3 })
            for (const auto gui : { false, true })
                if (!renderPitchMotionAudition(
                        motionDirectory, stringIndex, gui, timeline))
                    motionPassed = false;
    }
    if (!timeline)
        motionPassed = false;

    std::ofstream instructions(
        motionDirectory / "READ_ME_pitch_motion.txt");
    instructions
        << "LONG-FORM LISTENING CHECK / Fiddle Model\n"
        << "Files processor_pitch_motion_MIDI_G/D/A/E_* are actual MIDI Note Ons "
           "with C2 (Down Bow) held continuously.\n"
        << "Files processor_pitch_motion_GUI_G/D/A/E_* are generated with the "
           "Standalone Play Key Map's actual processor callbacks.\n"
        << "Stereo 16-bit PCM direct from FiddleModelAudioProcessor; "
           "no external oscillator, post EQ, timestretch, looping, or "
           "sample pitch-shift. Only clipping prevention at >0.92 peak.\n"
        << "Each phrase goes open, +5, +7, +2, open on the SAME "
           "physical string, about 1.45 seconds PER NOTE.\n"
        << "G string: MIDI 55 (G3), 60 (C4), 62 (D4), "
           "57 (A3), 55 (G3).\n"
        << "D string: MIDI 62 (D4), 67 (G4), 69 (A4), "
           "64 (E4), 62 (D4).\n"
        << "A string: MIDI 69 (A4), 74 (D5), 76 (E5), "
           "71 (B4), 69 (A4).\n"
        << "E string: MIDI 76 (E5), 81 (A5), 83 (B5), "
           "78 (F#5), 76 (E5).\n"
        << "Every 1.45 seconds the actual instrument is refingered while "
           "the same C2 bow is held. This is expressly for checking "
           "whether a stationary/raspy foreground masks the moving pitch.\n"
        << "Open processor_pitch_motion_timeline.csv for measured "
           "frequency, cents, low-three-harmonic fraction and exact "
           "step boundaries on the chosen physical string.\n"
        << "The timeline CSV includes brightness_proxy: RMS of the "
           "first sample difference divided by RMS signal. brightness_cv "
           "and rms_cv are the coefficients of variation across 50-ms "
           "windows after the onset. Very small brightness_cv signals "
           "an unusually static sustained spectrum, but these fields are "
           "AUDIT ONLY: no arbitrary threshold can prove fiddle realism.\n"
        << "Even if the automated pitch checks PASS, the report of "
           "separate bowed/pitched layers or buzzy synthetic timbre "
           "must be judged by hearing.\n";
    if (!instructions)
        motionPassed = false;

    // Render the full musical workflow at a fixed 48 kHz so the downloadable
    // MIDI and the CI listening artifact have one unambiguous timing reference.
    sampleRate = 48000.0;
    const bool musicalValidationPassed =
        renderFiddleValidationReel(motionDirectory);

    if (!uiPassed || !allStringsPassed || !motionPassed
        || !musicalValidationPassed)
    {
        if (!motionPassed)
            std::cerr << "FAIL: long-form pitch movement capture/regression\n";
        if (!musicalValidationPassed)
            std::cerr << "FAIL: 16-bar Fiddle Play musical validation reel\n";
        // Run the diagnostic whenever MIDI or GUI strict regression fails.
        // Re-run complete real Processor gestures with changes to the
        // *physical* bowing controls; these logs guide the next calibration
        // instead of guessing from indirect engine-only results.
        for (const auto rate : { 48000.0, 44100.0 })
        {
            sampleRate = rate;
            for (const auto stringIndex : { 0, 1 })
                for (const auto contact : { 0.27f, 0.34f, 0.41f, 0.48f })
                    for (const auto travel : { 0.38f, 0.50f, 0.62f })
                        for (const auto force : { 0.40f, 0.50f, 0.60f })
                            printGuiBowContactSweep(
                                stringIndex, force, travel, contact);
        }
        return EXIT_FAILURE;
    }

    std::cout
        << "PASS processor MIDI 48k and GUI click/drag 48k/44.1k on G/D/A/E\n";
    return EXIT_SUCCESS;
}
