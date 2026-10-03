#include "PluginEditor.h"

#include "ParameterPresentation.h"

#include <array>

HumanKnob::HumanKnob(juce::String title, juce::String explanation)
{
    title_.setText(std::move(title), juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    title_.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
    addAndMakeVisible(title_);

    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setMouseDragSensitivity(180);
    addAndMakeVisible(slider_);

    value_.setJustificationType(juce::Justification::centred);
    value_.setFont(juce::FontOptions(15.0f));
    addAndMakeVisible(value_);

    explanation_.setText(std::move(explanation), juce::dontSendNotification);
    explanation_.setJustificationType(juce::Justification::centredTop);
    explanation_.setFont(juce::FontOptions(12.0f));
    explanation_.setMinimumHorizontalScale(0.72f);
    explanation_.setColour(juce::Label::textColourId,
                           juce::Colours::white.withAlpha(0.66f));
    addAndMakeVisible(explanation_);

    slider_.setTooltip(explanation_.getText());
}

void HumanKnob::setValueText(const juce::String& text)
{
    value_.setText(text, juce::dontSendNotification);
}

void HumanKnob::setTitle(const juce::String& text)
{
    title_.setText(text, juce::dontSendNotification);
}

void HumanKnob::resized()
{
    auto area = getLocalBounds();
    title_.setBounds(area.removeFromTop(26));
    explanation_.setBounds(area.removeFromBottom(42));
    value_.setBounds(area.removeFromBottom(24));
    slider_.setBounds(area.reduced(8, 2));
}

FiddleModelAudioProcessorEditor::FiddleModelAudioProcessorEditor(
    FiddleModelAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      processor_(processor)
{
    setResizable(true, true);
    setResizeLimits(760, 430, 1180, 720);
    setSize(920, 520);

    title_.setText("Fiddle Model", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(28.0f).withStyle("Bold"));
    addAndMakeVisible(title_);

    subtitle_.setText(
        "Control it like a bow, not like a physics solver.",
        juce::dontSendNotification);
    subtitle_.setFont(juce::FontOptions(14.0f));
    subtitle_.setColour(juce::Label::textColourId,
                        juce::Colours::white.withAlpha(0.68f));
    addAndMakeVisible(subtitle_);

    addAndMakeVisible(bowGroup_);
    addAndMakeVisible(stringsGroup_);

    for (auto* component : std::array<juce::Component*, 8> {
             &pressure_, &speed_, &response_, &contact_,
             &focus_, &vibratoWidth_, &vibratoPace_, &bendRange_ })
        addAndMakeVisible(*component);

    auto& state = processor_.parameterState();
    pressureAttachment_ = std::make_unique<Attachment>(
        state, "pressure", pressure_.slider());
    speedAttachment_ = std::make_unique<Attachment>(
        state, "speed", speed_.slider());
    responseAttachment_ = std::make_unique<Attachment>(
        state, "attack", response_.slider());
    contactAttachment_ = std::make_unique<Attachment>(
        state, "position", contact_.slider());
    focusAttachment_ = std::make_unique<Attachment>(
        state, "balance", focus_.slider());
    vibratoWidthAttachment_ = std::make_unique<Attachment>(
        state, "vibratoWidth", vibratoWidth_.slider());
    vibratoPaceAttachment_ = std::make_unique<Attachment>(
        state, "vibratoPace", vibratoPace_.slider());
    bendRangeAttachment_ = std::make_unique<Attachment>(
        state, "bendRange", bendRange_.slider());

    pressure_.slider().onValueChange = [this] { refreshHumanReadableValues(); };
    speed_.slider().onValueChange = [this] { refreshHumanReadableValues(); };
    response_.slider().onValueChange = [this] { refreshHumanReadableValues(); };
    contact_.slider().onValueChange = [this] { refreshHumanReadableValues(); };
    focus_.slider().onValueChange = [this] { refreshHumanReadableValues(); };
    vibratoWidth_.slider().onValueChange = [this] { refreshHumanReadableValues(); };
    vibratoPace_.slider().onValueChange = [this] { refreshHumanReadableValues(); };
    bendRange_.slider().onValueChange = [this] { refreshHumanReadableValues(); };

    refreshHumanReadableValues();
    startTimerHz(10);
}

FiddleModelAudioProcessorEditor::~FiddleModelAudioProcessorEditor()
{
    stopTimer();
}

void FiddleModelAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(28, 30, 34));

    const auto top = getLocalBounds().removeFromTop(78).toFloat();
    g.setColour(juce::Colour::fromRGB(38, 41, 47));
    g.fillRect(top);

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawLine(0.0f, top.getBottom(), static_cast<float>(getWidth()), top.getBottom());
}

void FiddleModelAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(18);

    auto header = area.removeFromTop(54);
    title_.setBounds(header.removeFromLeft(230));
    subtitle_.setBounds(header);

    area.removeFromTop(18);

    const auto bowHeight = static_cast<int>(area.getHeight() * 0.56f);
    auto bowArea = area.removeFromTop(bowHeight);
    bowGroup_.setBounds(bowArea);

    auto bowContent = bowArea.reduced(14, 30);
    const auto bowWidth = bowContent.getWidth() / 4;
    pressure_.setBounds(bowContent.removeFromLeft(bowWidth));
    speed_.setBounds(bowContent.removeFromLeft(bowWidth));
    response_.setBounds(bowContent.removeFromLeft(bowWidth));
    contact_.setBounds(bowContent);

    area.removeFromTop(10);
    stringsGroup_.setBounds(area);

    auto stringsContent = area.reduced(14, 30);
    const auto column = stringsContent.getWidth() / 4;
    focus_.setBounds(stringsContent.removeFromLeft(column));
    vibratoWidth_.setBounds(stringsContent.removeFromLeft(column));
    vibratoPace_.setBounds(stringsContent.removeFromLeft(column));
    bendRange_.setBounds(stringsContent);
}

void FiddleModelAudioProcessorEditor::timerCallback()
{
    refreshHumanReadableValues();
}

void FiddleModelAudioProcessorEditor::refreshHumanReadableValues()
{
    pressure_.setValueText(
        fiddle::presentation::bowPressure(
            static_cast<float>(pressure_.slider().getValue())));
    speed_.setValueText(
        fiddle::presentation::bowSpeed(
            static_cast<float>(speed_.slider().getValue())));
    response_.setValueText(
        fiddle::presentation::bowResponse(
            static_cast<float>(response_.slider().getValue())));
    contact_.setValueText(
        fiddle::presentation::bowContact(
            static_cast<float>(contact_.slider().getValue())));
    static constexpr std::array<const char*, 3> pairNames {
        "G \u2194 D", "D \u2194 A", "A \u2194 E"
    };
    const auto pair = juce::jlimit(0, 2, processor_.activePairLowerString());
    focus_.setTitle("String Focus  " + juce::String::fromUTF8(pairNames[static_cast<std::size_t>(pair)]));
    focus_.setValueText(
        fiddle::presentation::stringFocus(
            static_cast<float>(focus_.slider().getValue())));
    vibratoWidth_.setValueText(
        fiddle::presentation::vibratoWidth(
            static_cast<float>(vibratoWidth_.slider().getValue())));
    vibratoPace_.setValueText(
        fiddle::presentation::vibratoPace(
            static_cast<float>(vibratoPace_.slider().getValue())));
    bendRange_.setValueText(
        fiddle::presentation::bendRange(
            static_cast<float>(bendRange_.slider().getValue())));
}
