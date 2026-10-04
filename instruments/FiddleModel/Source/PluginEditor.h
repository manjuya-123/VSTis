#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>

class HumanKnob final : public juce::Component
{
public:
    HumanKnob(juce::String title, juce::String explanation);

    juce::Slider& slider() noexcept { return slider_; }
    void setValueText(const juce::String& text);
    void setTitle(const juce::String& text);

    void resized() override;

private:
    juce::Label title_;
    juce::Slider slider_;
    juce::Label value_;
    juce::Label explanation_;
};

class InstrumentView final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    void setState(FiddleVisualState state);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    std::function<void(float)> onBowContactChanged;
    std::function<void(float)> onStringFocusChanged;

private:
    void applyGesture(juce::Point<float> position);
    FiddleVisualState state_{};
};

class PlayKeyMap final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    void setState(int playMode,
                  int bowAction,
                  int fingeringMidiNote,
                  std::uint64_t fingeringMask);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<void(int, bool)> onActionKey;

private:
    int actionKeyAt(juce::Point<float>) const noexcept;

    int playMode_ = 0;
    int bowAction_ = 0;
    int fingeringMidiNote_ = -1;
    std::uint64_t fingeringMask_ = 0;
    int mouseActionKey_ = -1;
};

class FiddleModelAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit FiddleModelAudioProcessorEditor(FiddleModelAudioProcessor&);
    ~FiddleModelAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void refreshHumanReadableValues();
    void timerCallback() override;

    FiddleModelAudioProcessor& processor_;

    juce::Label title_;
    juce::Label subtitle_;
    juce::Label outputLevelLabel_;
    juce::Slider outputLevel_;
    InstrumentView instrumentView_;
    PlayKeyMap playKeyMap_;
    juce::GroupComponent bowGroup_ { "bow", "Bow" };
    juce::GroupComponent stringsGroup_ { "strings", "Strings & Pitch" };
    juce::GroupComponent materialsGroup_ { "materials", "Materials" };
    juce::Label playModeLabel_;
    juce::ComboBox playMode_;
    juce::Label playModeGuide_;
    juce::ToggleButton fingeringHoldButton_ { "Fingering Hold" };
    juce::Label strokeLabel_;
    juce::ComboBox strokeMode_;
    juce::Label bodyMaterialLabel_;
    juce::ComboBox bodyMaterial_;
    juce::Label bowMaterialLabel_;
    juce::ComboBox bowMaterial_;
    juce::Label contactMaterialLabel_;
    juce::ComboBox contactMaterial_;
    juce::Label stringMaterialLabel_;
    juce::ComboBox stringMaterial_;
    juce::TooltipWindow tooltips_ { this, 500 };

    HumanKnob pressure_ {
        "Bow Pressure",
        "How firmly the bow presses into the strings."
    };
    HumanKnob speed_ {
        "Bow Speed",
        "How quickly the bow moves across the strings."
    };
    HumanKnob response_ {
        "Bow Response",
        "Soft gives a gradual start; Crisp catches the string quickly."
    };
    HumanKnob contact_ {
        "Bow Contact",
        "Fingerboard side is warmer; bridge side is brighter and more biting."
    };
    HumanKnob focus_ {
        "String Focus",
        "Aim the bow toward the lower or upper string of the active adjacent pair."
    };
    HumanKnob vibratoWidth_ {
        "Vibrato Width",
        "How far the left hand rocks the stopped pitch."
    };
    HumanKnob vibratoPace_ {
        "Vibrato Pace",
        "How quickly the left hand rocks back and forth."
    };
    HumanKnob bendRange_ {
        "Pitch Bend Range",
        "How far the MIDI pitch wheel can move the fingered pitch."
    };

    std::unique_ptr<Attachment> pressureAttachment_;
    std::unique_ptr<Attachment> speedAttachment_;
    std::unique_ptr<Attachment> responseAttachment_;
    std::unique_ptr<Attachment> contactAttachment_;
    std::unique_ptr<Attachment> focusAttachment_;
    std::unique_ptr<Attachment> vibratoWidthAttachment_;
    std::unique_ptr<Attachment> vibratoPaceAttachment_;
    std::unique_ptr<Attachment> bendRangeAttachment_;
    std::unique_ptr<Attachment> outputLevelAttachment_;
    std::unique_ptr<ComboAttachment> playModeAttachment_;
    std::unique_ptr<ButtonAttachment> fingeringHoldAttachment_;
    std::unique_ptr<ComboAttachment> strokeModeAttachment_;
    std::unique_ptr<ComboAttachment> bodyMaterialAttachment_;
    std::unique_ptr<ComboAttachment> bowMaterialAttachment_;
    std::unique_ptr<ComboAttachment> contactMaterialAttachment_;
    std::unique_ptr<ComboAttachment> stringMaterialAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FiddleModelAudioProcessorEditor)
};
