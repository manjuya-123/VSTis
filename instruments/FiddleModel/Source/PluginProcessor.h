#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/FiddleEngine.h"
#include "Dsp/MidiNoteStack.h"

#include <atomic>

struct FiddleVisualState
{
    bool active = false;
    int midiNote = -1;
    int primaryString = 1;
    int pairLowerString = 1;
    int bowDirection = 1;
    float speakingFrequencyHz = 293.6648f;
    float bowContact = 0.5f;
    float stringFocus = 0.0f;
};

class FiddleModelAudioProcessor final : public juce::AudioProcessor
{
public:
    FiddleModelAudioProcessor();
    ~FiddleModelAudioProcessor() override = default;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& parameterState() noexcept { return parameters_; }
    int activePairLowerString() const noexcept { return activePairLowerString_.load(std::memory_order_relaxed); }
    FiddleVisualState visualState() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static float midiNoteToHz(int midiNote);
    static int pairForMidiNote(int midiNote) noexcept;
    float bentFrequencyForNote(int midiNote) const;
    void applyPerformanceControls() noexcept;
    fiddle::FiddleEngine engine_;
    fiddle::MidiNoteStack noteStack_;
    juce::AudioProcessorValueTreeState parameters_;
    float pitchWheelNormalized_ = 0.0f;
    float pitchBendRangeSemitones_ = 2.0f;
    float modWheelNormalized_ = 0.0f;
    float channelPressureNormalized_ = 0.0f;
    fiddle::Controls baseControls_{};
    std::atomic<int> activePairLowerString_ { 1 };
    std::atomic<int> activeMidiNote_ { -1 };
    std::atomic<int> visualPrimaryString_ { 1 };
    std::atomic<int> visualBowDirection_ { 1 };
    std::atomic<float> visualSpeakingFrequencyHz_ { 293.6648f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FiddleModelAudioProcessor)
};
