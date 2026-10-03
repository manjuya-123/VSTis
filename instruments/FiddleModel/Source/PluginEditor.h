#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

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

class InstrumentView final : public juce::Component
{
public:
    void setState(FiddleVisualState state);
    void paint(juce::Graphics&) override;

private:
    FiddleVisualState state_{};
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

    void refreshHumanReadableValues();
    void timerCallback() override;

    FiddleModelAudioProcessor& processor_;

    juce::Label title_;
    juce::Label subtitle_;
    InstrumentView instrumentView_;
    juce::GroupComponent bowGroup_ { "bow", "Bow" };
    juce::GroupComponent stringsGroup_ { "strings", "Strings & Pitch" };
    juce::GroupComponent materialsGroup_ { "materials", "Materials" };
    juce::Label strokeLabel_;
    juce::ComboBox strokeMode_;
    juce::Label bodyMaterialLabel_;
    juce::ComboBox bodyMaterial_;
    juce::Label bowMaterialLabel_;
    juce::ComboBox bowMaterial_;
    juce::Label contactMaterialLabel_;
    juce::ComboBox contactMaterial_;
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
    std::unique_ptr<ComboAttachment> strokeModeAttachment_;
    std::unique_ptr<ComboAttachment> bodyMaterialAttachment_;
    std::unique_ptr<ComboAttachment> bowMaterialAttachment_;
    std::unique_ptr<ComboAttachment> contactMaterialAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FiddleModelAudioProcessorEditor)
};
