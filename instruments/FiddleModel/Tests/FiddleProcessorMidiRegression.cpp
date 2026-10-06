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

bool renderPitchMotionAudition(const std::filesystem::path& outputDirectory,
                              int stringIndex,
                              bool gui,
                              std::ofstream& timeline)
{
    const int openNote = stringIndex == 0 ? 55 : 62;
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

        const bool noteValid = state.midiNote == note
            && state.primaryString == stringIndex
            && std::isfinite(cents)
            && std::abs(cents) <= 10.0;
        passed &= noteValid;

        const double beginning = firstFrame / sampleRate;
        const double ending = recording.left.size() / sampleRate;
        timeline << (gui ? "GUI" : "MIDI") << ','
                 << (stringIndex == 0 ? 'G' : 'D') << ','
                 << sampleRate << ',' << i << ',' << note << ','
                 << target << ',' << measured << ',' << cents << ','
                 << beginning << ',' << ending << ','
                 << state.primaryString << ','
                 << (noteValid ? "PASS" : "FAIL") << '\n';
        std::cout << "processor_pitch_motion"
                  << " route=" << (gui ? "GUI" : "MIDI")
                  << " string=" << (stringIndex == 0 ? "G" : "D")
                  << " rate=" << sampleRate
                  << " note=" << note
                  << " duration=" << (ending - beginning)
                  << " target=" << target
                  << " measured=" << measured
                  << " cents=" << cents
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
            + (stringIndex == 0 ? "G_" : "D_")
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

    if (!uiPassed || !allStringsPassed)
    {
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
