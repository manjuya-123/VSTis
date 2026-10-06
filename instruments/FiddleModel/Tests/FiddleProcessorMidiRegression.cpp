#include "PluginProcessor.h"

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
constexpr double sampleRate = 48000.0;
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

// Fifth-related G/D/A/E fundamentals share real harmonics at e.g. 588 Hz
// (G3 harmonic 3 == D4 harmonic 2). Those shared partials cannot prove that
// either string is being bowed. Compare only the non-overlapping comb lines
// when evaluating unwanted neighbour dominance. The same +8 dB requirement
// still applies to the string-specific spectral evidence.
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

        bool overlapsOther = false;
        for (int otherHarmonic = 1; otherHarmonic <= 8; ++otherHarmonic)
        {
            const auto otherFrequency = otherFundamental * otherHarmonic;
            if (std::abs(frequency - otherFrequency)
                <= std::max(2.0, 0.005 * frequency))
            {
                overlapsOther = true;
                break;
            }
        }
        if (!overlapsOther)
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
              << '\\n';
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
    double lowThreePower = 0.0;
    for (int harmonic = 1; harmonic <= 8; ++harmonic)
    {
        const auto power = tonePower(segment, target * harmonic);
        firstEightPower += power;
        if (harmonic <= 3)
            lowThreePower += power;
    }
    const auto lowThreeFraction =
        lowThreePower / (firstEightPower + 1.0e-30);

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
        && (stringIndex > 1 || lowThreeFraction >= 0.25);
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
} // namespace

int main(int argc, char** argv)
{
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

    if (!allStringsPassed)
        return EXIT_FAILURE;

    std::cout
        << "PASS processor bow-first fingering regression on G/D/A/E\n";
    return EXIT_SUCCESS;
}
