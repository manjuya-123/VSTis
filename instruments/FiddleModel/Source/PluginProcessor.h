#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/FiddleEngine.h"
#include "Dsp/MidiNoteStack.h"

#include <array>
#include <atomic>

struct FiddleVisualState
{
    bool active = false;
    int midiNote = -1;
    int primaryString = 1;
    int pairLowerString = 1;
    int bowDirection = 1;
    int playMode = 0;
    int bowAction = 0;
    bool fingeringHold = false;
    std::array<float, 4> speakingFrequencyHz {
        195.9977f, 293.6648f, 440.0f, 659.2551f
    };
    std::array<float, 4> contactTemperatureC {
        20.0f, 20.0f, 20.0f, 20.0f
    };
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
    void requestPlayActionFromUi(int midiNote, bool pressed) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static float midiNoteToHz(int midiNote);
    static int pairForMidiNote(int midiNote) noexcept;
    float bentFrequencyForNote(int midiNote) const;
    void applyPerformanceControls() noexcept;
    void updateFiddlePlayFingering();
    void triggerFiddlePlayAction(int midiNote, float velocity);
    void releaseFiddlePlayAction(int midiNote);
    void resetPerformanceModeState() noexcept;
    void updateFingeringHoldState(bool enabled);
    fiddle::FiddleEngine engine_;
    fiddle::MidiNoteStack noteStack_;
    juce::AudioProcessorValueTreeState parameters_;
    float pitchWheelNormalized_ = 0.0f;
    float pitchBendRangeSemitones_ = 2.0f;
    float modWheelNormalized_ = 0.0f;
    float channelPressureNormalized_ = 0.0f;
    fiddle::Controls baseControls_{};
    int lastPlayMode_ = -1;
    int activeBowActionNote_ = -1;
    int playBowDirection_ = 1;
    bool playModeFocusOverride_ = false;
    bool fingeringHold_ = false;
    bool fingeringPedalHold_ = false;
    std::array<bool, 128> fingeringKeyDown_{};
    float playModeFocusValue_ = 0.0f;
    float playModePressureBoost_ = 0.0f;
    float playModeSpeedScale_ = 1.0f;
    float playModeGestureStrength_ = 0.5f;
    int playModePreferredPrimaryString_ = -1;
    std::atomic<int> activePairLowerString_ { 1 };
    std::atomic<int> activeMidiNote_ { -1 };
    std::atomic<int> visualPrimaryString_ { 1 };
    std::atomic<int> visualBowDirection_ { 1 };
    std::atomic<int> visualPlayMode_ { 0 };
    std::atomic<int> visualBowAction_ { 0 };
    std::atomic<bool> visualFingeringHold_ { false };
    std::atomic<int> pendingUiActionPress_ { -1 };
    std::atomic<int> pendingUiActionRelease_ { -1 };
    std::array<std::atomic<float>, 4> visualSpeakingFrequencyHz_{};
    std::array<std::atomic<float>, 4> visualContactTemperatureC_{};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FiddleModelAudioProcessor)
};
