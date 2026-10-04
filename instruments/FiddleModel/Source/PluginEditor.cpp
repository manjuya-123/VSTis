#include "PluginEditor.h"

#include "ParameterPresentation.h"
#include "Dsp/FiddlePlayLayout.h"
#include "Dsp/detail/ModelConstants.h"

#include <algorithm>
#include <array>

namespace
{
juce::String bowActionName(int actionValue)
{
    const auto action = static_cast<fiddle::BowAction>(actionValue);
    switch (action)
    {
        case fiddle::BowAction::DownBow: return "Down Bow";
        case fiddle::BowAction::UpBow: return "Up Bow";
        case fiddle::BowAction::ShortStroke: return "Short Stroke";
        case fiddle::BowAction::Tremolo: return "Tremolo";
        case fiddle::BowAction::DroneBow: return "Drone Bow";
        case fiddle::BowAction::AccentStroke: return "Accent Stroke";
        case fiddle::BowAction::Chop: return "Chop";
        case fiddle::BowAction::Release: return "Release";
        case fiddle::BowAction::Shuffle: return "Shuffle";
        case fiddle::BowAction::None: break;
    }
    return {};
}

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


void PlayKeyMap::setState(int playMode,
                          int bowAction,
                          int fingeringMidiNote,
                          std::uint64_t fingeringMask)
{
    playMode_ = playMode;
    bowAction_ = bowAction;
    fingeringMidiNote_ = fingeringMidiNote;
    fingeringMask_ = fingeringMask;
    repaint();
}

void PlayKeyMap::paint(juce::Graphics& g)
{
    auto outer = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour::fromRGB(22, 24, 28));
    g.fillRoundedRectangle(outer, 9.0f);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(outer, 9.0f, 1.0f);

    const bool fiddlePlay =
        playMode_ == static_cast<int>(fiddle::PlayMode::FiddlePlay);

    auto area = outer.reduced(7.0f);
    auto titleArea = area.removeFromTop(20.0f);
    g.setColour(juce::Colours::white.withAlpha(fiddlePlay ? 0.88f : 0.42f));
    g.setFont(juce::FontOptions(11.5f).withStyle("Bold"));
    g.drawText(
        "PLAY KEY MAP   |   low keys = bow hand   |   G3+ = left hand   |   CC64 = Fingering Hold",
        titleArea.toNearestInt(), juce::Justification::centredLeft);

    const auto actionWidth = area.getWidth() * 0.38f;
    const auto unusedWidth = area.getWidth() * 0.13f;
    const auto gap = 7.0f;

    auto actionPanel = juce::Rectangle<float>(
        area.getX(), area.getY(),
        actionWidth - gap, area.getHeight());
    auto unusedPanel = juce::Rectangle<float>(
        actionPanel.getRight() + gap, area.getY(),
        unusedWidth - gap, area.getHeight());
    auto fingeringPanel = juce::Rectangle<float>(
        unusedPanel.getRight() + gap, area.getY(),
        area.getRight() - unusedPanel.getRight() - gap,
        area.getHeight());

    const auto drawPanelHeading =
        [&g, fiddlePlay](juce::Rectangle<float> panel,
                         const juce::String& text)
        {
            g.setColour(juce::Colours::white.withAlpha(
                fiddlePlay ? 0.78f : 0.36f));
            g.setFont(juce::FontOptions(10.5f).withStyle("Bold"));
            g.drawText(text,
                       panel.removeFromTop(18.0f).toNearestInt(),
                       juce::Justification::centred);
        };

    drawPanelHeading(actionPanel, "C2-B2   BOW ACTIONS");
    drawPanelHeading(unusedPanel, "UNUSED");
    drawPanelHeading(fingeringPanel, "G3-C8   FINGERING");

    auto actionKeyboard = actionPanel.withTrimmedTop(18.0f);
    auto unusedBody = unusedPanel.withTrimmedTop(18.0f);
    auto fingeringKeyboard = fingeringPanel.withTrimmedTop(18.0f);

    g.setColour(juce::Colour::fromRGB(35, 38, 44));
    g.fillRoundedRectangle(unusedBody, 4.0f);
    g.setColour(juce::Colours::white.withAlpha(fiddlePlay ? 0.42f : 0.22f));
    g.setFont(juce::FontOptions(10.0f));
    g.drawFittedText(
        "C3-F#3\nno Play-Mode role",
        unusedBody.reduced(4.0f).toNearestInt(),
        juce::Justification::centred, 2);

    const auto isBlackKey = [](int note) noexcept
    {
        const auto pitchClass = note % 12;
        return pitchClass == 1 || pitchClass == 3
            || pitchClass == 6 || pitchClass == 8
            || pitchClass == 10;
    };

    const auto actionShortName = [](fiddle::BowAction action)
    {
        switch (action)
        {
            case fiddle::BowAction::DownBow: return juce::String("Down");
            case fiddle::BowAction::Shuffle: return juce::String("Shuffle");
            case fiddle::BowAction::UpBow: return juce::String("Up");
            case fiddle::BowAction::ShortStroke: return juce::String("Short");
            case fiddle::BowAction::Tremolo: return juce::String("Tremolo");
            case fiddle::BowAction::DroneBow: return juce::String("Drone");
            case fiddle::BowAction::AccentStroke: return juce::String("Accent");
            case fiddle::BowAction::Chop: return juce::String("Chop");
            case fiddle::BowAction::Release: return juce::String("Release");
            case fiddle::BowAction::None: break;
        }
        return juce::String();
    };

    const auto drawKeyboard =
        [&](juce::Rectangle<float> keyboard,
            int firstNote,
            int lastNote,
            bool actionRange)
        {
            int whiteCount = 0;
            for (int note = firstNote; note <= lastNote; ++note)
                if (!isBlackKey(note))
                    ++whiteCount;

            if (whiteCount <= 0)
                return;

            const auto whiteWidth =
                keyboard.getWidth() / static_cast<float>(whiteCount);

            const auto whitesBefore = [&](int note)
            {
                int count = 0;
                for (int n = firstNote; n < note; ++n)
                    if (!isBlackKey(n))
                        ++count;
                return count;
            };

            for (int note = firstNote; note <= lastNote; ++note)
            {
                if (isBlackKey(note))
                    continue;

                const auto whiteIndex = whitesBefore(note);
                auto key = juce::Rectangle<float>(
                    keyboard.getX()
                        + whiteWidth * static_cast<float>(whiteIndex),
                    keyboard.getY(),
                    whiteWidth,
                    keyboard.getHeight());

                const auto action = fiddle::bowActionForMidiNote(note);
                const bool assigned =
                    actionRange ? action != fiddle::BowAction::None
                                : fiddle::isFingeringKey(note);
                const bool actionActive =
                    fiddlePlay && actionRange
                    && bowAction_ == static_cast<int>(action)
                    && action != fiddle::BowAction::None;
                const bool fingeringActive =
                    fiddlePlay && !actionRange
                    && (fingeringMask_
                        & fiddle::fingeringMaskBit(note)) != 0;
                const bool fingeringPrimary =
                    fingeringActive && note == fingeringMidiNote_;

                if (actionActive)
                    g.setColour(juce::Colour::fromRGB(80, 159, 180));
                else if (fingeringPrimary)
                    g.setColour(juce::Colour::fromRGB(246, 190, 82));
                else if (fingeringActive)
                    g.setColour(juce::Colour::fromRGB(224, 157, 79));
                else if (assigned && fiddlePlay)
                    g.setColour(actionRange
                        ? juce::Colour::fromRGB(205, 215, 220)
                        : juce::Colour::fromRGB(218, 207, 170));
                else
                    g.setColour(juce::Colour::fromRGB(174, 178, 184));

                g.fillRect(key);
                g.setColour(juce::Colours::black.withAlpha(0.48f));
                g.drawRect(key, 0.8f);

                if (actionRange && assigned)
                {
                    auto textArea = key.reduced(1.5f);
                    auto noteArea = textArea.removeFromBottom(13.0f);
                    g.setColour(juce::Colours::black.withAlpha(0.82f));
                    g.setFont(juce::FontOptions(8.0f).withStyle("Bold"));
                    g.drawText(
                        midiNoteName(note),
                        noteArea.toNearestInt(),
                        juce::Justification::centred);
                    g.setFont(juce::FontOptions(8.5f));
                    g.drawFittedText(
                        actionShortName(action),
                        textArea.toNearestInt(),
                        juce::Justification::centredBottom, 1);
                }
                else if (!actionRange
                         && (note == firstNote
                             || note == lastNote
                             || note % 12 == 0))
                {
                    g.setColour(juce::Colours::black.withAlpha(0.62f));
                    g.setFont(juce::FontOptions(7.5f));
                    g.drawFittedText(
                        midiNoteName(note),
                        key.reduced(1.0f).toNearestInt(),
                        juce::Justification::centredBottom, 1);
                }
            }

            const auto blackWidth = whiteWidth * 0.62f;
            const auto blackHeight = keyboard.getHeight() * 0.60f;

            for (int note = firstNote; note <= lastNote; ++note)
            {
                if (!isBlackKey(note))
                    continue;

                const auto whiteBoundary = whitesBefore(note);
                auto key = juce::Rectangle<float>(
                    keyboard.getX()
                        + whiteWidth * static_cast<float>(whiteBoundary)
                        - blackWidth * 0.5f,
                    keyboard.getY(),
                    blackWidth,
                    blackHeight);

                const auto action = fiddle::bowActionForMidiNote(note);
                const bool assigned =
                    actionRange ? action != fiddle::BowAction::None
                                : fiddle::isFingeringKey(note);
                const bool actionActive =
                    fiddlePlay && actionRange
                    && bowAction_ == static_cast<int>(action)
                    && action != fiddle::BowAction::None;
                const bool fingeringActive =
                    fiddlePlay && !actionRange
                    && (fingeringMask_
                        & fiddle::fingeringMaskBit(note)) != 0;
                const bool fingeringPrimary =
                    fingeringActive && note == fingeringMidiNote_;

                if (actionActive)
                    g.setColour(juce::Colour::fromRGB(69, 145, 166));
                else if (fingeringPrimary)
                    g.setColour(juce::Colour::fromRGB(225, 148, 57));
                else if (fingeringActive)
                    g.setColour(juce::Colour::fromRGB(176, 104, 51));
                else if (assigned && fiddlePlay)
                    g.setColour(actionRange
                        ? juce::Colour::fromRGB(53, 69, 78)
                        : juce::Colour::fromRGB(73, 66, 49));
                else
                    g.setColour(juce::Colour::fromRGB(42, 45, 51));

                g.fillRoundedRectangle(key, 2.0f);
                g.setColour(juce::Colours::white.withAlpha(
                    assigned && fiddlePlay ? 0.38f : 0.18f));
                g.drawRoundedRectangle(key, 2.0f, 0.8f);

                if (actionRange && assigned)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.94f));
                    g.setFont(juce::FontOptions(7.0f).withStyle("Bold"));
                    g.drawFittedText(
                        midiNoteName(note) + "\n" + actionShortName(action),
                        key.reduced(1.0f).toNearestInt(),
                        juce::Justification::centredBottom, 2);
                }
            }
        };

    drawKeyboard(actionKeyboard, 36, 47, true);
    drawKeyboard(
        fingeringKeyboard,
        fiddle::fiddleLowestNote,
        fiddle::fiddleHighestNote,
        false);
}

int PlayKeyMap::actionKeyAt(juce::Point<float> position) const noexcept
{
    if (playMode_ != static_cast<int>(fiddle::PlayMode::FiddlePlay))
        return -1;

    auto outer = getLocalBounds().toFloat().reduced(2.0f);
    auto area = outer.reduced(7.0f);
    area.removeFromTop(20.0f);

    const auto actionWidth = area.getWidth() * 0.38f;
    const auto gap = 7.0f;
    auto actionPanel = juce::Rectangle<float>(
        area.getX(), area.getY(),
        actionWidth - gap, area.getHeight());
    auto keyboard = actionPanel.withTrimmedTop(18.0f);

    const auto isBlackKey = [](int note) noexcept
    {
        const auto pitchClass = note % 12;
        return pitchClass == 1 || pitchClass == 3
            || pitchClass == 6 || pitchClass == 8
            || pitchClass == 10;
    };

    constexpr int firstNote = 36;
    constexpr int lastNote = 47;
    constexpr int whiteCount = 7;
    const auto whiteWidth =
        keyboard.getWidth() / static_cast<float>(whiteCount);
    const auto blackWidth = whiteWidth * 0.62f;
    const auto blackHeight = keyboard.getHeight() * 0.60f;

    const auto whitesBefore = [&](int note)
    {
        int count = 0;
        for (int n = firstNote; n < note; ++n)
            if (!isBlackKey(n))
                ++count;
        return count;
    };

    for (int note = firstNote; note <= lastNote; ++note)
    {
        if (!isBlackKey(note))
            continue;

        const auto boundary = whitesBefore(note);
        const auto key = juce::Rectangle<float>(
            keyboard.getX()
                + whiteWidth * static_cast<float>(boundary)
                - blackWidth * 0.5f,
            keyboard.getY(), blackWidth, blackHeight);

        if (key.contains(position))
            return fiddle::isBowActionKey(note) ? note : -1;
    }

    for (int note = firstNote; note <= lastNote; ++note)
    {
        if (isBlackKey(note))
            continue;

        const auto whiteIndex = whitesBefore(note);
        const auto key = juce::Rectangle<float>(
            keyboard.getX()
                + whiteWidth * static_cast<float>(whiteIndex),
            keyboard.getY(), whiteWidth, keyboard.getHeight());

        if (key.contains(position)
            && fiddle::isBowActionKey(note))
            return note;
    }

    return -1;
}

void PlayKeyMap::mouseDown(const juce::MouseEvent& event)
{
    mouseActionKey_ = actionKeyAt(event.position);
    if (mouseActionKey_ >= 0 && onActionKey)
        onActionKey(mouseActionKey_, true);
}

void PlayKeyMap::mouseUp(const juce::MouseEvent&)
{
    if (mouseActionKey_ >= 0 && onActionKey)
        onActionKey(mouseActionKey_, false);

    mouseActionKey_ = -1;
}


void InstrumentView::mouseDown(const juce::MouseEvent& event)
{
    applyGesture(event.position);
}

void InstrumentView::mouseDrag(const juce::MouseEvent& event)
{
    applyGesture(event.position);
}

void InstrumentView::applyGesture(juce::Point<float> position)
{
    const auto bounds = getLocalBounds().toFloat().reduced(6.0f);
    const auto nutX = bounds.getX() + 76.0f;
    const auto bridgeX = bounds.getRight() - 44.0f;
    const auto topY = bounds.getY() + 44.0f;
    const auto bottomY = bounds.getBottom() - 28.0f;
    const auto spacing = (bottomY - topY) / 3.0f;

    const auto x = juce::jlimit(nutX, bridgeX, position.x);
    const auto beta = (bridgeX - x) / (bridgeX - nutX);
    const auto contact = juce::jlimit(
        0.0f, 1.0f,
        static_cast<float>(
            (fiddle::detail::bowBetaFingerboard - beta)
            / (fiddle::detail::bowBetaFingerboard - fiddle::detail::bowBetaBridge)));

    const auto pair = juce::jlimit(0, 2, state_.pairLowerString);
    const auto pairCenterY = topY + spacing * (static_cast<float>(pair) + 0.5f);
    const auto focus = juce::jlimit(
        -1.0f, 1.0f, (position.y - pairCenterY) / (0.5f * spacing));

    if (onBowContactChanged)
        onBowContactChanged(contact);
    if (onStringFocusChanged)
        onStringFocusChanged(focus);
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

    for (int stringIndex = 0; stringIndex < 4; ++stringIndex)
    {
        const auto openHz = openFrequency[static_cast<std::size_t>(stringIndex)];
        const auto speakingHz =
            state_.speakingFrequencyHz[static_cast<std::size_t>(stringIndex)];
        const auto ratio = speakingHz > openHz * 1.0005f
            ? 1.0f - openHz / speakingHz
            : 0.0f;
        const auto fingerFraction = juce::jlimit(0.0f, 0.78f, ratio);

        if (fingerFraction > 0.002f)
        {
            const auto y = topY + spacing * static_cast<float>(stringIndex);
            const auto fingerX = nutX + fingerFraction * (bridgeX - nutX);

            g.setColour(stringIndex == primary
                ? juce::Colour::fromRGB(245, 132, 105)
                : juce::Colour::fromRGB(212, 112, 96));
            g.fillEllipse(fingerX - 7.0f, y - 7.0f, 14.0f, 14.0f);

            g.setColour(juce::Colours::white.withAlpha(0.62f));
            g.drawLine(fingerX, y - 15.0f, fingerX, y + 15.0f, 1.1f);
        }
    }

    const auto beta = static_cast<float>(
        fiddle::detail::bowBetaFingerboard
        + (fiddle::detail::bowBetaBridge - fiddle::detail::bowBetaFingerboard)
        * juce::jlimit(0.0f, 1.0f, state_.bowContact));
    const auto bowX = bridgeX - beta * (bridgeX - nutX);
    const auto pair = juce::jlimit(0, 2, state_.pairLowerString);
    const auto pairTopY = topY + spacing * static_cast<float>(pair) - 22.0f;
    const auto pairBottomY = topY + spacing * static_cast<float>(pair + 1) + 22.0f;
    const auto tilt = juce::jlimit(-1.0f, 1.0f, state_.stringFocus) * 14.0f;

    const auto lowerTemperature =
        state_.contactTemperatureC[static_cast<std::size_t>(pair)];
    const auto upperTemperature =
        state_.contactTemperatureC[static_cast<std::size_t>(pair + 1)];
    const auto contactTemperature =
        std::max(lowerTemperature, upperTemperature);
    const auto heat = juce::jlimit(
        0.0f, 1.0f, (contactTemperature - 28.0f) / 42.0f);
    const auto coolBow = juce::Colour::fromRGB(107, 195, 214);
    const auto hotBow = juce::Colour::fromRGB(238, 132, 88);
    const auto bowColour = coolBow.interpolatedWith(hotBow, heat);

    g.setColour(bowColour);
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
    const auto actionText = bowActionName(state_.bowAction);
    const auto rosinState =
        contactTemperature < 34.0f ? juce::String("Rosin Cool")
      : contactTemperature < 56.0f ? juce::String("Rosin Working")
                                    : juce::String("Rosin Hot");

    juce::String status =
        noteText + " on " + stringText + " string  |  " + rosinState;
    if (state_.playMode == static_cast<int>(fiddle::PlayMode::FiddlePlay))
    {
        status = juce::String("Fiddle Play  |  ") + status;
        if (state_.fingeringHold)
            status += "  |  Fingering Hold";
        if (actionText.isNotEmpty())
            status += juce::String("  |  ") + actionText;
    }
    else
    {
        status += juce::String("  |  ") + juce::String(directionText);
    }

    g.setColour(juce::Colours::white.withAlpha(0.82f));
    g.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
    g.drawText(status,
               getLocalBounds().removeFromTop(30).reduced(18, 0),
               juce::Justification::centredRight);
}


HumanKnob::HumanKnob(juce::String title, juce::String explanation)
{
    title_.setText(std::move(title), juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    title_.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    addAndMakeVisible(title_);

    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setMouseDragSensitivity(180);
    addAndMakeVisible(slider_);

    value_.setJustificationType(juce::Justification::centred);
    value_.setFont(juce::FontOptions(13.5f));
    addAndMakeVisible(value_);

    explanation_.setText(std::move(explanation), juce::dontSendNotification);
    explanation_.setJustificationType(juce::Justification::centredTop);
    explanation_.setFont(juce::FontOptions(12.0f));
    explanation_.setMinimumHorizontalScale(0.72f);
    explanation_.setColour(juce::Label::textColourId,
                           juce::Colours::white.withAlpha(0.66f));
    // Keep the explanation in the tooltip instead of permanently spending
    // vertical panel space on prose.
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
    title_.setBounds(area.removeFromTop(22));
    value_.setBounds(area.removeFromBottom(21));
    slider_.setBounds(area.reduced(6, 1));
}

FiddleModelAudioProcessorEditor::FiddleModelAudioProcessorEditor(
    FiddleModelAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      processor_(processor)
{
    setResizable(true, true);
    setResizeLimits(860, 820, 1320, 1080);
    setSize(1020, 900);

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

    outputLevelLabel_.setText("Output", juce::dontSendNotification);
    outputLevelLabel_.setJustificationType(juce::Justification::centredRight);
    outputLevelLabel_.setColour(
        juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
    addAndMakeVisible(outputLevelLabel_);

    outputLevel_.setSliderStyle(juce::Slider::LinearHorizontal);
    outputLevel_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 20);
    outputLevel_.setNumDecimalPlacesToDisplay(1);
    outputLevel_.setTextValueSuffix(" dB");
    outputLevel_.setTooltip(
        "Final post-model level. This changes loudness only and does not feed back into the bow, strings, or body model.");
    addAndMakeVisible(outputLevel_);

    playModeLabel_.setText("Play Mode", juce::dontSendNotification);
    playModeLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(playModeLabel_);

    playMode_.addItem("Chromatic (keyboard)", 1);
    playMode_.addItem("Fiddle Play (performance)", 2);
    playMode_.setTooltip(
        "Chromatic behaves like a normal MIDI instrument. Fiddle Play separates left-hand fingering from right-hand bow actions.");
    addAndMakeVisible(playMode_);

    playModeGuide_.setText(
        "Fiddle Play uses two hands: the Key Map below shows bow commands and the G3+ fingering region.",
        juce::dontSendNotification);
    playModeGuide_.setFont(juce::FontOptions(12.5f));
    playModeGuide_.setColour(juce::Label::textColourId,
                             juce::Colours::white.withAlpha(0.70f));
    addAndMakeVisible(playModeGuide_);

    fingeringHoldButton_.setTooltip(
        "Latch the current left-hand fingering. Sustain pedal (CC64) controls the same effective hold state.");
    addAndMakeVisible(fingeringHoldButton_);

    addAndMakeVisible(instrumentView_);
    addAndMakeVisible(playKeyMap_);
    playKeyMap_.setTooltip(
        "Fiddle Play map. Click the C2-B2 bow-action keys; G3-C8 shows the left-hand fingering region.");
    playKeyMap_.onActionKey = [this](int midiNote, bool pressed)
    {
        processor_.requestPlayActionFromUi(midiNote, pressed);
    };
    instrumentView_.setTooltip(
        "Drag left/right to move the bow between fingerboard and bridge. Drag toward either string to focus that string.");
    instrumentView_.onBowContactChanged = [this](float value)
    {
        contact_.slider().setValue(value, juce::sendNotificationSync);
    };
    instrumentView_.onStringFocusChanged = [this](float value)
    {
        focus_.slider().setValue(value, juce::sendNotificationSync);
    };

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
        "Changes static grip, sliding friction, contact-state relaxation and microscopic hair/rosin roughness inside the bow-string contact. These are physical-model profiles rather than EQ/noise presets.");
    addAndMakeVisible(contactMaterial_);

    stringMaterialLabel_.setText("String Core", juce::dontSendNotification);
    stringMaterialLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(stringMaterialLabel_);
    stringMaterial_.addItem("Synthetic core", 1);
    stringMaterial_.addItem("Steel core", 2);
    stringMaterial_.addItem("Gut-like", 3);
    stringMaterial_.setTooltip(
        "Changes distributed string loss and phase dispersion. Steel is the quickest profile; Gut-like is softer and slower. Profiles are directional, not brand calibrations.");
    addAndMakeVisible(stringMaterial_);

    contact_.slider().setSliderStyle(juce::Slider::LinearHorizontal);
    focus_.slider().setSliderStyle(juce::Slider::LinearHorizontal);
    bendRange_.slider().setSliderStyle(juce::Slider::LinearHorizontal);

    for (auto* component : std::array<juce::Component*, 8> {
             &pressure_, &speed_, &response_, &contact_,
             &focus_, &vibratoWidth_, &vibratoPace_, &bendRange_ })
        addAndMakeVisible(*component);

    auto& state = processor_.parameterState();
    playModeAttachment_ = std::make_unique<ComboAttachment>(
        state, "playMode", playMode_);
    fingeringHoldAttachment_ = std::make_unique<ButtonAttachment>(
        state, "fingeringHold", fingeringHoldButton_);
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
    outputLevelAttachment_ = std::make_unique<Attachment>(
        state, "outputLevelDb", outputLevel_);
    strokeModeAttachment_ = std::make_unique<ComboAttachment>(
        state, "strokeMode", strokeMode_);
    bodyMaterialAttachment_ = std::make_unique<ComboAttachment>(
        state, "bodyMaterial", bodyMaterial_);
    bowMaterialAttachment_ = std::make_unique<ComboAttachment>(
        state, "bowMaterial", bowMaterial_);
    contactMaterialAttachment_ = std::make_unique<ComboAttachment>(
        state, "contactMaterial", contactMaterial_);
    stringMaterialAttachment_ = std::make_unique<ComboAttachment>(
        state, "stringMaterial", stringMaterial_);

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

    auto outputArea = header.removeFromRight(235);
    outputLevelLabel_.setBounds(outputArea.removeFromLeft(62));
    outputLevel_.setBounds(outputArea.reduced(4, 12));

    subtitle_.setBounds(header);

    area.removeFromTop(12);

    instrumentView_.setBounds(area.removeFromTop(158));
    area.removeFromTop(6);

    auto playModeRow = area.removeFromTop(40);
    playModeLabel_.setBounds(playModeRow.removeFromLeft(110));
    playMode_.setBounds(playModeRow.removeFromLeft(180).reduced(4, 7));
    fingeringHoldButton_.setBounds(
        playModeRow.removeFromLeft(150).reduced(8, 9));
    playModeGuide_.setBounds(playModeRow.reduced(8, 3));

    playKeyMap_.setBounds(area.removeFromTop(108).reduced(4, 2));
    area.removeFromTop(6);

    const auto bowHeight = 170;
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
    auto stringsArea = area.removeFromTop(135);
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
    const auto materialColumn = materials.getWidth() / 4;

    auto bodyArea = materials.removeFromLeft(materialColumn);
    bodyMaterialLabel_.setBounds(bodyArea.removeFromTop(24));
    bodyMaterial_.setBounds(bodyArea.removeFromTop(32).reduced(6, 2));

    auto bowArea2 = materials.removeFromLeft(materialColumn);
    bowMaterialLabel_.setBounds(bowArea2.removeFromTop(24));
    bowMaterial_.setBounds(bowArea2.removeFromTop(32).reduced(6, 2));

    auto contactArea = materials.removeFromLeft(materialColumn);
    contactMaterialLabel_.setBounds(contactArea.removeFromTop(24));
    contactMaterial_.setBounds(contactArea.removeFromTop(32).reduced(6, 2));

    stringMaterialLabel_.setBounds(materials.removeFromTop(24));
    stringMaterial_.setBounds(materials.removeFromTop(32).reduced(6, 2));
}

void FiddleModelAudioProcessorEditor::timerCallback()
{
    const auto state = processor_.visualState();
    instrumentView_.setState(state);
    playKeyMap_.setState(
        state.playMode,
        state.bowAction,
        state.midiNote,
        state.fingeringMask);

    const bool fiddlePlay =
        state.playMode == static_cast<int>(fiddle::PlayMode::FiddlePlay);
    strokeMode_.setEnabled(!fiddlePlay);
    strokeLabel_.setAlpha(fiddlePlay ? 0.38f : 1.0f);
    fingeringHoldButton_.setEnabled(fiddlePlay);
    fingeringHoldButton_.setAlpha(fiddlePlay ? 1.0f : 0.38f);
    playModeGuide_.setAlpha(fiddlePlay ? 1.0f : 0.42f);

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
        "G <-> D", "D <-> A", "A <-> E"
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
