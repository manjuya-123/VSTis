#include "PluginProcessor.h"

#include <cmath>

FiddleModelAudioProcessor::FiddleModelAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

void FiddleModelAudioProcessor::prepareToPlay(double sampleRate, int) { engine_.prepare(sampleRate); }

void FiddleModelAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn()) engine_.noteOn(midiNoteToHz(message.getNoteNumber()), message.getFloatVelocity());
        else if (message.isNoteOff() || message.isAllNotesOff() || message.isAllSoundOff()) engine_.noteOff();
    }

    fiddle::Controls c;
    c.pressure = parameters_.getRawParameterValue("pressure")->load();
    c.speed = parameters_.getRawParameterValue("speed")->load();
    c.attack = parameters_.getRawParameterValue("attack")->load();
    c.position = parameters_.getRawParameterValue("position")->load();
    c.balance = parameters_.getRawParameterValue("balance")->load();
    engine_.setControls(c);

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;
    engine_.process(left, right, static_cast<std::size_t>(buffer.getNumSamples()));
}

juce::AudioProcessorEditor* FiddleModelAudioProcessor::createEditor() { return new juce::GenericAudioProcessorEditor(*this); }

void FiddleModelAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = parameters_.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void FiddleModelAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters_.state.getType())) parameters_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorValueTreeState::ParameterLayout FiddleModelAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("pressure", "Pressure", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("speed", "Speed", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("attack", "Attack", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("position", "Position", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("balance", "Balance", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    return { params.begin(), params.end() };
}

float FiddleModelAudioProcessor::midiNoteToHz(int midiNote)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FiddleModelAudioProcessor(); }
