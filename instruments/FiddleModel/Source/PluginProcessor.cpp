#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

FiddleModelAudioProcessor::FiddleModelAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

void FiddleModelAudioProcessor::prepareToPlay(double sampleRate, int)
{
    noteStack_.reset();
    pitchWheelNormalized_ = 0.0f;
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
    pitchBendRangeSemitones_ =
        parameters_.getRawParameterValue("bendRange")->load();
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
            const auto selection = noteStack_.noteOn(
                message.getNoteNumber(), message.getFloatVelocity());
            activePairLowerString_.store(
                pairForMidiNote(selection.note), std::memory_order_relaxed);
            engine_.noteOn(bentFrequencyForNote(selection.note), selection.velocity);
        }
        else if (message.isNoteOff())
        {
            const auto selection = noteStack_.noteOff(message.getNoteNumber());
            if (selection.changed)
            {
                if (selection.active)
                {
                    activePairLowerString_.store(
                        pairForMidiNote(selection.note), std::memory_order_relaxed);
                    engine_.noteOn(bentFrequencyForNote(selection.note), selection.velocity);
                }
                else
                {
                    engine_.noteOff();
                }
            }
        }
        else if (message.isPitchWheel())
        {
            const auto value = message.getPitchWheelValue();
            pitchWheelNormalized_ = value >= 8192
                ? static_cast<float>(value - 8192) / 8191.0f
                : static_cast<float>(value - 8192) / 8192.0f;

            const auto selection = noteStack_.current();
            if (selection.active)
                engine_.retune(bentFrequencyForNote(selection.note));
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            noteStack_.reset();
            engine_.noteOff();
        }
    }

    renderUntil(blockSize);
}

juce::AudioProcessorEditor* FiddleModelAudioProcessor::createEditor()
{
    return new FiddleModelAudioProcessorEditor(*this);
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
        "pressure", "Bow Pressure", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "speed", "Bow Speed", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Bow Response", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "position", "Bow Contact", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "balance", "String Focus", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bendRange", "Pitch Bend Range",
        juce::NormalisableRange<float>(1.0f, 24.0f, 1.0f), 2.0f));

    return { params.begin(), params.end() };
}

float FiddleModelAudioProcessor::midiNoteToHz(int midiNote)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

int FiddleModelAudioProcessor::pairForMidiNote(int midiNote) noexcept
{
    if (midiNote < 62) return 0; // G-D
    if (midiNote < 69) return 1; // D-A
    return 2;                    // A-E
}

float FiddleModelAudioProcessor::bentFrequencyForNote(int midiNote) const
{
    const auto semitones =
        pitchWheelNormalized_ * pitchBendRangeSemitones_;
    return midiNoteToHz(midiNote)
        * std::pow(2.0f, semitones / 12.0f);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FiddleModelAudioProcessor();
}
