#include "PluginEditor.h"

#include "ParameterPresentation.h"

#include <array>

namespace
{
juce::String midiNoteName(int note)
{
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    if (note < 0)
        return {};
    const auto pitchClass = note % 12;
    const auto octave = note / 12 - 1;
    return juce::String(names[static_cast<std::size_t>(pitchClass)])
         + juce::String(octave);
}
}

void InstrumentView::setState(FiddleVisualState state)
{
    state_ = state;
    repaint();
}

void InstrumentView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(6.0f);
    g.setColour(juce::Colour::fromRGB(22, 24, 28));
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);

    const auto nutX = bounds.getX() + 76.0f;
    const auto bridgeX = bounds.getRight() - 44.0f;
    const auto topY = bounds.getY() + 44.0f;
    const auto bottomY = bounds.getBottom() - 28.0f;
    const auto spacing = (bottomY - topY) / 3.0f;

    static constexpr std::array<const char*, 4> stringNames { "G", "D", "A", "E" };
    static constexpr std::array<float, 4> openFrequency {
        195.9977f, 293.6648f, 440.0f, 659.2551f
    };

    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.drawLine(nutX, topY - 16.0f, nutX, bottomY + 16.0f, 2.0f);
    g.drawLine(bridgeX, topY - 18.0f, bridgeX, bottomY + 18.0f, 3.0f);

    for (int i = 0; i < 4; ++i)
    {
        const auto y = topY + spacing * static_cast<float>(i);
        const bool inPair =
            i == state_.pairLowerString || i == state_.pairLowerString + 1;
        const bool primary = i == state_.primaryString;

        if (primary)
            g.setColour(juce::Colour::fromRGB(242, 190, 92));
        else if (inPair)
            g.setColour(juce::Colours::white.withAlpha(0.72f));
        else
            g.setColour(juce::Colours::white.withAlpha(0.28f));

        g.drawLine(nutX, y, bridgeX, y, primary ? 3.0f : 1.4f);

        g.setFont(juce::FontOptions(14.0f).withStyle(primary ? "Bold" : "Regular"));
        g.drawText(stringNames[static_cast<std::size_t>(i)],
                   24, static_cast<int>(y - 10.0f), 30, 20,
                   juce::Justification::centred);
    }

    if (!state_.active)
    {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(juce::FontOptions(15.0f));
        g.drawText("Play MIDI to see fingering and bow motion",
                   getLocalBounds().removeFromTop(30).reduced(18, 0),
                   juce::Justification::centredRight);
        return;
    }

    const auto primary = juce::jlimit(0, 3, state_.primaryString);
    const auto primaryY = topY + spacing * static_cast<float>(primary);
    const auto openHz = openFrequency[static_cast<std::size_t>(primary)];
    const auto ratio = state_.speakingFrequencyHz > 1.0f
        ? 1.0f - openHz / state_.speakingFrequencyHz
        : 0.0f;
    const auto fingerFraction = juce::jlimit(0.0f, 0.78f, ratio);

    if (fingerFraction > 0.002f)
    {
        const auto fingerX = nutX + fingerFraction * (bridgeX - nutX);
        g.setColour(juce::Colour::fromRGB(235, 126, 104));
        g.fillEllipse(fingerX - 7.0f, primaryY - 7.0f, 14.0f, 14.0f);
        g.setColour(juce::Colours::white.withAlpha(0.72f));
        g.drawLine(fingerX, primaryY - 17.0f, fingerX, primaryY + 17.0f, 1.2f);
    }

    const auto beta = 0.22f + (0.06f - 0.22f)
                    * juce::jlimit(0.0f, 1.0f, state_.bowContact);
    const auto bowX = bridgeX - beta * (bridgeX - nutX);
    const auto pair = juce::jlimit(0, 2, state_.pairLowerString);
    const auto pairTopY = topY + spacing * static_cast<float>(pair) - 22.0f;
    const auto pairBottomY = topY + spacing * static_cast<float>(pair + 1) + 22.0f;
    const auto tilt = juce::jlimit(-1.0f, 1.0f, state_.stringFocus) * 14.0f;

    g.setColour(juce::Colour::fromRGB(107, 195, 214));
    g.drawLine(bowX - tilt, pairTopY, bowX + tilt, pairBottomY, 4.0f);

    juce::Path arrow;
    if (state_.bowDirection >= 0)
    {
        arrow.addTriangle(bowX - tilt - 7.0f, pairTopY + 9.0f,
                          bowX - tilt + 7.0f, pairTopY + 9.0f,
                          bowX - tilt, pairTopY - 3.0f);
    }
    else
    {
        arrow.addTriangle(bowX + tilt - 7.0f, pairBottomY - 9.0f,
                          bowX + tilt + 7.0f, pairBottomY - 9.0f,
                          bowX + tilt, pairBottomY + 3.0f);
    }
    g.fillPath(arrow);

    const auto noteText = midiNoteName(state_.midiNote);
    const auto stringText = juce::String(stringNames[static_cast<std::size_t>(primary)]);
    const auto directionText = state_.bowDirection >= 0 ? "Down bow" : "Up bow";

    g.setColour(juce::Colours::white.withAlpha(0.82f));
    g.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
    g.drawText(noteText + " on " + stringText + " string  |  " + directionText,
               getLocalBounds().removeFromTop(30).reduced(18, 0),
               juce::Justification::centredRight);
}


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
    setResizeLimits(840, 720, 1280, 980);
    setSize(980, 820);

    title_.setText("Fiddle Model", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(28.0f).withStyle("Bold"));
    addAndMakeVisible(title_);

    subtitle_.setText(
        "Play the gesture, not the solver.  Mod Wheel: Vibrato  |  Aftertouch: Bow Pressure",
        juce::dontSendNotification);
    subtitle_.setFont(juce::FontOptions(14.0f));
    subtitle_.setColour(juce::Label::textColourId,
                        juce::Colours::white.withAlpha(0.68f));
    addAndMakeVisible(subtitle_);

    addAndMakeVisible(instrumentView_);
    addAndMakeVisible(bowGroup_);
    addAndMakeVisible(stringsGroup_);
    addAndMakeVisible(materialsGroup_);

    strokeLabel_.setText("Bow Strokes", juce::dontSendNotification);
    strokeLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(strokeLabel_);
    strokeMode_.addItem("Fiddle Auto", 1);
    strokeMode_.addItem("Connected", 2);
    strokeMode_.addItem("Alternate", 3);
    strokeMode_.setTooltip(
        "Fiddle Auto alternates separate notes but keeps overlapping legato notes on the same bow. Connected never auto-reverses. Alternate reverses on every Note On.");
    addAndMakeVisible(strokeMode_);

    bodyMaterialLabel_.setText("Body", juce::dontSendNotification);
    bodyMaterialLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bodyMaterialLabel_);
    bodyMaterial_.addItem("Traditional spruce/maple", 1);
    bodyMaterial_.addItem("Light stiff composite", 2);
    bodyMaterial_.addItem("Dense experimental", 3);
    bodyMaterial_.addItem("Rigid composite", 4);
    bodyMaterial_.setTooltip(
        "Changes the body's modal stiffness, damping and mechanical admittance. Experimental profiles are model profiles, not measured instrument brands.");
    addAndMakeVisible(bodyMaterial_);

    bowMaterialLabel_.setText("Bow Stick", juce::dontSendNotification);
    bowMaterialLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bowMaterialLabel_);
    bowMaterial_.addItem("Pernambuco-like", 1);
    bowMaterial_.addItem("Carbon-like", 2);
    bowMaterial_.addItem("Light rigid experimental", 3);
    bowMaterial_.addItem("Flexible experimental", 4);
    bowMaterial_.setTooltip(
        "Changes the effective bow-stick response seen by the player's acceleration and reversals.");
    addAndMakeVisible(bowMaterial_);

    contactMaterialLabel_.setText("Hair / Rosin", juce::dontSendNotification);
    contactMaterialLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(contactMaterialLabel_);
    contactMaterial_.addItem("Horsehair + medium rosin", 1);
    contactMaterial_.addItem("Dry / light grip", 2);
    contactMaterial_.addItem("High-grip rosin", 3);
    contactMaterial_.addItem("Synthetic hair", 4);
    contactMaterial_.setTooltip(
        "Changes static grip, sliding friction and the contact-state relaxation. These are physical-model profiles rather than EQ presets.");
    addAndMakeVisible(contactMaterial_);

    contact_.slider().setSliderStyle(juce::Slider::LinearHorizontal);
    focus_.slider().setSliderStyle(juce::Slider::LinearHorizontal);
    bendRange_.slider().setSliderStyle(juce::Slider::LinearHorizontal);

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
    strokeModeAttachment_ = std::make_unique<ComboAttachment>(
        state, "strokeMode", strokeMode_);
    bodyMaterialAttachment_ = std::make_unique<ComboAttachment>(
        state, "bodyMaterial", bodyMaterial_);
    bowMaterialAttachment_ = std::make_unique<ComboAttachment>(
        state, "bowMaterial", bowMaterial_);
    contactMaterialAttachment_ = std::make_unique<ComboAttachment>(
        state, "contactMaterial", contactMaterial_);

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

    area.removeFromTop(12);

    instrumentView_.setBounds(area.removeFromTop(180));
    area.removeFromTop(10);

    const auto bowHeight = 235;
    auto bowArea = area.removeFromTop(bowHeight);
    bowGroup_.setBounds(bowArea);

    auto bowContent = bowArea.reduced(14, 30);
    auto strokeRow = bowContent.removeFromBottom(34);
    strokeLabel_.setBounds(strokeRow.removeFromLeft(130));
    strokeMode_.setBounds(strokeRow.removeFromLeft(180).reduced(4, 3));

    const auto bowWidth = bowContent.getWidth() / 4;
    pressure_.setBounds(bowContent.removeFromLeft(bowWidth));
    speed_.setBounds(bowContent.removeFromLeft(bowWidth));
    response_.setBounds(bowContent.removeFromLeft(bowWidth));
    contact_.setBounds(bowContent);

    area.removeFromTop(10);
    auto stringsArea = area.removeFromTop(170);
    stringsGroup_.setBounds(stringsArea);

    auto stringsContent = stringsArea.reduced(14, 30);
    const auto column = stringsContent.getWidth() / 4;
    focus_.setBounds(stringsContent.removeFromLeft(column));
    vibratoWidth_.setBounds(stringsContent.removeFromLeft(column));
    vibratoPace_.setBounds(stringsContent.removeFromLeft(column));
    bendRange_.setBounds(stringsContent);

    area.removeFromTop(10);
    materialsGroup_.setBounds(area);

    auto materials = area.reduced(14, 28);
    const auto materialColumn = materials.getWidth() / 3;

    auto bodyArea = materials.removeFromLeft(materialColumn);
    bodyMaterialLabel_.setBounds(bodyArea.removeFromTop(24));
    bodyMaterial_.setBounds(bodyArea.removeFromTop(32).reduced(6, 2));

    auto bowArea2 = materials.removeFromLeft(materialColumn);
    bowMaterialLabel_.setBounds(bowArea2.removeFromTop(24));
    bowMaterial_.setBounds(bowArea2.removeFromTop(32).reduced(6, 2));

    contactMaterialLabel_.setBounds(materials.removeFromTop(24));
    contactMaterial_.setBounds(materials.removeFromTop(32).reduced(6, 2));
}

void FiddleModelAudioProcessorEditor::timerCallback()
{
    instrumentView_.setState(processor_.visualState());
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
