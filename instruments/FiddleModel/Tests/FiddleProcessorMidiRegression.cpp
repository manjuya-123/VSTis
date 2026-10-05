#include "PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
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

double estimateDominantPitch(const std::vector<float>& x)
{
    // Search the whole G-string first-position region rather than only a
    // narrow window around the expected note. The previous local estimator
    // could "find" a weak moving stopped component while a much louder stale
    // open string remained the perceptual main pitch.
    constexpr int candidates = 1200;
    constexpr double lowHz = 185.0;
    constexpr double highHz = 305.0;
    const auto length = std::min<std::size_t>(
        x.size(), static_cast<std::size_t>(0.12 * sampleRate));
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

bool checkPitch(const std::vector<float>& segment, int note)
{
    const auto target = static_cast<double>(midiToHz(note));
    const auto measured = estimateDominantPitch(segment);
    const auto cents = centsBetween(measured, target);
    std::cout << "processor_bow_first_dominant_pitch note=" << note
              << " target=" << target
              << " measured=" << measured
              << " cents=" << cents << '\n';
    return std::abs(cents) <= 10.0;
}
} // namespace

int main()
{
    FiddleModelAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);

    // Exact reported workflow: hold C2 first, then move the left-hand
    // fingering through G3..C#4. C2 must remain a live bow action even when
    // fingering keys have small key-up gaps between them.
    const auto downBow = juce::MidiMessage::noteOn(1, 36, 0.85f);
    std::vector<float> scratch;
    renderBlocks(processor, 4, scratch, &downBow);

    // Bow-first is armed, not assigned to an arbitrary default string.
    // Until the first fingering arrives there should be no radiated note.
    float armedPeak = 0.0f;
    for (const auto sample : scratch)
        armedPeak = std::max(armedPeak, std::abs(sample));
    if (armedPeak > 1.0e-5f)
    {
        std::cerr << "FAIL: bow-first action excited a default string before fingering"
                  << " peak=" << armedPeak << '\n';
        return EXIT_FAILURE;
    }

    auto playNote = [&](int note, bool overlapPrevious, int previousNote)
    {
        juce::AudioBuffer<float> eventBuffer(2, blockSize);
        juce::MidiBuffer events;

        if (overlapPrevious)
        {
            events.addEvent(juce::MidiMessage::noteOn(1, note, 0.82f), 0);
            if (previousNote >= 0)
                events.addEvent(juce::MidiMessage::noteOff(1, previousNote), 32);
        }
        else
        {
            if (previousNote >= 0)
                events.addEvent(juce::MidiMessage::noteOff(1, previousNote), 0);
            events.addEvent(juce::MidiMessage::noteOn(1, note, 0.82f), 96);
        }

        processor.processBlock(eventBuffer, events);

        std::vector<float> segment;
        const auto* first = eventBuffer.getReadPointer(0);
        segment.insert(segment.end(), first, first + blockSize);
        renderBlocks(processor, 52, segment);

        const auto state = processor.visualState();
        if (state.primaryString != 0)
        {
            std::cerr << "FAIL: bow-first G-string phrase left primary string\n";
            return false;
        }
        if (state.stringFocus > -0.85f)
        {
            std::cerr << "FAIL: bow-first fingering did not reapply single-string focus"
                      << " focus=" << state.stringFocus << '\n';
            return false;
        }
        if (std::abs(
                state.speakingFrequencyHz[0]
                - midiToHz(note)) > 2.0f)
        {
            std::cerr << "FAIL: visual/engine speaking frequency did not follow MIDI"
                      << " note=" << note
                      << " speaking=" << state.speakingFrequencyHz[0] << '\n';
            return false;
        }
        if (!checkPitch(segment, note))
        {
            std::cerr << "FAIL: radiated pitch did not follow bow-first fingering\n";
            return false;
        }
        return true;
    };

    if (!playNote(55, false, -1))
        return EXIT_FAILURE;
    if (!playNote(56, false, 55))
        return EXIT_FAILURE;
    if (!playNote(57, true, 56))
        return EXIT_FAILURE;
    if (!playNote(61, true, 57))
        return EXIT_FAILURE;

    juce::AudioBuffer<float> releaseBuffer(2, blockSize);
    juce::MidiBuffer releaseMidi;
    releaseMidi.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
    processor.processBlock(releaseBuffer, releaseMidi);

    std::cout << "PASS processor bow-first fingering regression\n";
    return EXIT_SUCCESS;
}
