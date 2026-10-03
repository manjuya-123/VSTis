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

    void refreshHumanReadableValues();
    void timerCallback() override;

    FiddleModelAudioProcessor& processor_;

    juce::Label title_;
    juce::Label subtitle_;
    juce::GroupComponent bowGroup_ { "bow", "Bow" };
    juce::GroupComponent stringsGroup_ { "strings", "Strings & Pitch" };
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
    HumanKnob bendRange_ {
        "Pitch Bend Range",
        "How far the MIDI pitch wheel can move the fingered pitch."
    };

    std::unique_ptr<Attachment> pressureAttachment_;
    std::unique_ptr<Attachment> speedAttachment_;
    std::unique_ptr<Attachment> responseAttachment_;
    std::unique_ptr<Attachment> contactAttachment_;
    std::unique_ptr<Attachment> focusAttachment_;
    std::unique_ptr<Attachment> bendRangeAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FiddleModelAudioProcessorEditor)
};
