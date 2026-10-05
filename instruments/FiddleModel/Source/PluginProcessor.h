#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Dsp/FiddleEngine.h"
#include "Dsp/MidiNoteStack.h"

#include <array>
#include <atomic>
#include <cstdint>

struct FiddleVisualState
{
    bool active = false;
    int midiNote = -1;
    int lastInputMidiNote = -1;
    int primaryString = 1;
    int pairLowerString = 1;
    int bowDirection = 1;
    int playMode = 0;
    int bowAction = 0;
    bool fingeringHold = false;
    std::uint64_t fingeringMask = 0;
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
    void requestPlayFingeringFromUi(int midiNote, bool pressed) noexcept;

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
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        outputGainLinear_ { 1.0f };
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
    bool playModeAutoFocusEnabled_ = false;
    bool fingeringHold_ = false;
    bool fingeringPedalHold_ = false;
    std::array<bool, 128> fingeringKeyDown_{};
    float playModeFocusValue_ = 0.0f;
    float playModeAutoFocusValue_ = 0.0f;
    float playModePressureBoost_ = 0.0f;
    float playModeSpeedScale_ = 1.0f;
    float playModeResponseBoost_ = 0.0f;
    bool playModeOneShotLatched_ = false;
    bool playModeBowArmed_ = false;
    // If a bow starts from one fingering note, overlapping MIDI note-ons during
    // that bow are treated as melodic left-hand slurs rather than accidental
    // double stops. A multi-note shape prepared before the bow remains
    // explicitly polyphonic.
    bool playModeMonophonicPhrase_ = false;
    float playModeGestureStrength_ = 0.5f;
    int playModePreferredPrimaryString_ = -1;
    std::atomic<int> activePairLowerString_ { 1 };
    std::atomic<int> activeMidiNote_ { -1 };
    std::atomic<int> visualPrimaryString_ { 1 };
    std::atomic<int> visualBowDirection_ { 1 };
    std::atomic<int> visualPlayMode_ { 0 };
    std::atomic<int> visualBowAction_ { 0 };
    std::atomic<float> visualEffectiveStringFocus_ { 0.0f };
    std::atomic<bool> visualFingeringHold_ { false };
    std::atomic<std::uint64_t> visualFingeringMask_ { 0 };
    std::atomic<int> pendingUiActionPress_ { -1 };
    std::atomic<int> pendingUiActionRelease_ { -1 };
    std::atomic<int> pendingUiFingeringPress_ { -1 };
    std::atomic<int> pendingUiFingeringRelease_ { -1 };
    std::atomic<int> visualLastInputMidiNote_ { -1 };
    std::array<std::atomic<float>, 4> visualSpeakingFrequencyHz_{};
    std::array<std::atomic<float>, 4> visualContactTemperatureC_{};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FiddleModelAudioProcessor)
};
