#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

FiddleModelAudioProcessor::FiddleModelAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

void FiddleModelAudioProcessor::prepareToPlay(double sampleRate, int)
{
    engine_.prepare(sampleRate);
}

void FiddleModelAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    fiddle::Controls controls;
    controls.pressure = parameters_.getRawParameterValue("pressure")->load();
    controls.speed = parameters_.getRawParameterValue("speed")->load();
    controls.attack = parameters_.getRawParameterValue("attack")->load();
    controls.position = parameters_.getRawParameterValue("position")->load();
    controls.balance = parameters_.getRawParameterValue("balance")->load();
    engine_.setControls(controls);

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;
    const auto blockSize = buffer.getNumSamples();
    int renderedUntil = 0;

    const auto renderUntil = [&](int endSample)
    {
        endSample = std::clamp(endSample, renderedUntil, blockSize);
        const auto count = endSample - renderedUntil;
        if (count > 0)
        {
            engine_.process(left + renderedUntil,
                            right + renderedUntil,
                            static_cast<std::size_t>(count));
            renderedUntil = endSample;
        }
    };

    // Render around MIDI event offsets so bow/finger transitions begin at the
    // correct sample inside the host block rather than at the block boundary.
    for (const auto metadata : midi)
    {
        renderUntil(metadata.samplePosition);

        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            engine_.noteOn(midiNoteToHz(message.getNoteNumber()),
                           message.getFloatVelocity());
        }
        else if (message.isNoteOff() || message.isAllNotesOff() || message.isAllSoundOff())
        {
            engine_.noteOff();
        }
    }

    renderUntil(blockSize);
}

juce::AudioProcessorEditor* FiddleModelAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void FiddleModelAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = parameters_.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void FiddleModelAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters_.state.getType()))
            parameters_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorValueTreeState::ParameterLayout
FiddleModelAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pressure", "Pressure", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "speed", "Speed", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "position", "Position", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "balance", "Balance", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));

    return { params.begin(), params.end() };
}

float FiddleModelAudioProcessor::midiNoteToHz(int midiNote)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FiddleModelAudioProcessor();
}
