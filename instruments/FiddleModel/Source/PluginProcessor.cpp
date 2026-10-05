#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Dsp/BowStrokePolicy.h"
#include "Dsp/FiddleFingeringVoicer.h"
#include "Dsp/FiddlePlayLayout.h"
#include "Dsp/FiddleGestureProfile.h"

#include <algorithm>
#include <array>
#include <cmath>

FiddleModelAudioProcessor::FiddleModelAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    static constexpr std::array<float, 4> openHz {
        195.9977f, 293.6648f, 440.0f, 659.2551f
    };
    for (std::size_t i = 0; i < openHz.size(); ++i)
    {
        visualSpeakingFrequencyHz_[i].store(openHz[i], std::memory_order_relaxed);
        visualContactTemperatureC_[i].store(20.0f, std::memory_order_relaxed);
    }
}

void FiddleModelAudioProcessor::prepareToPlay(double sampleRate, int)
{
    pitchWheelNormalized_ = 0.0f;
    modWheelNormalized_ = 0.0f;
    channelPressureNormalized_ = 0.0f;

    engine_.prepare(sampleRate);
    outputGainLinear_.reset(sampleRate, 0.020);
    outputGainLinear_.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(
            parameters_.getRawParameterValue("outputLevelDb")->load()));
    noteStack_.reset();
    lastPlayMode_ = -1;
    resetPerformanceModeState();
}

void FiddleModelAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    baseControls_.pressure = parameters_.getRawParameterValue("pressure")->load();
    baseControls_.speed = parameters_.getRawParameterValue("speed")->load();
    baseControls_.attack = parameters_.getRawParameterValue("attack")->load();
    baseControls_.position = parameters_.getRawParameterValue("position")->load();
    baseControls_.balance = parameters_.getRawParameterValue("balance")->load();
    baseControls_.vibratoWidth = parameters_.getRawParameterValue("vibratoWidth")->load();
    baseControls_.vibratoPace = parameters_.getRawParameterValue("vibratoPace")->load();
    pitchBendRangeSemitones_ =
        parameters_.getRawParameterValue("bendRange")->load();
    outputGainLinear_.setTargetValue(
        juce::Decibels::decibelsToGain(
            parameters_.getRawParameterValue("outputLevelDb")->load()));

    fiddle::MaterialSettings materials;
    materials.body = static_cast<fiddle::BodyMaterialPreset>(
        static_cast<int>(parameters_.getRawParameterValue("bodyMaterial")->load()));
    materials.bowStick = static_cast<fiddle::BowStickPreset>(
        static_cast<int>(parameters_.getRawParameterValue("bowMaterial")->load()));
    materials.contact = static_cast<fiddle::ContactMaterialPreset>(
        static_cast<int>(parameters_.getRawParameterValue("contactMaterial")->load()));
    materials.strings = static_cast<fiddle::StringCorePreset>(
        static_cast<int>(parameters_.getRawParameterValue("stringMaterial")->load()));
    engine_.setMaterials(materials);

    const auto playMode = static_cast<int>(
        parameters_.getRawParameterValue("playMode")->load());

    if (playMode != lastPlayMode_)
    {
        noteStack_.reset();
        resetPerformanceModeState();
        lastPlayMode_ = playMode;
        visualPlayMode_.store(playMode, std::memory_order_relaxed);
    }

    if (playMode == static_cast<int>(fiddle::PlayMode::FiddlePlay))
    {
        const bool parameterHold =
            parameters_.getRawParameterValue("fingeringHold")->load() >= 0.5f;
        updateFingeringHoldState(parameterHold || fingeringPedalHold_);
    }

    applyPerformanceControls();

    if (playMode == static_cast<int>(fiddle::PlayMode::FiddlePlay))
    {
        const auto uiPress =
            pendingUiActionPress_.exchange(-1, std::memory_order_relaxed);
        if (uiPress >= 0)
            triggerFiddlePlayAction(uiPress, 0.82f);

        const auto uiRelease =
            pendingUiActionRelease_.exchange(-1, std::memory_order_relaxed);
        if (uiRelease >= 0)
            releaseFiddlePlayAction(uiRelease);
    }

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;
    const auto numBlockSamples = buffer.getNumSamples();
    int renderedUntil = 0;

    const auto renderUntil = [&](int endSample)
    {
        endSample = std::clamp(endSample, renderedUntil, numBlockSamples);
        const auto count = endSample - renderedUntil;
        if (count > 0)
        {
            engine_.process(left + renderedUntil,
                            right + renderedUntil,
                            static_cast<std::size_t>(count));
            renderedUntil = endSample;
        }
    };

    for (const auto metadata : midi)
    {
        renderUntil(metadata.samplePosition);

        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            const auto note = message.getNoteNumber();
            const auto velocity = message.getFloatVelocity();

            if (playMode == static_cast<int>(fiddle::PlayMode::FiddlePlay))
            {
                if (fiddle::isBowActionKey(note))
                {
                    triggerFiddlePlayAction(note, velocity);
                }
                else if (fiddle::isFingeringKey(note))
                {
                    fingeringKeyDown_[static_cast<std::size_t>(note)] = true;
                    noteStack_.noteOn(note, velocity);
                    updateFiddlePlayFingering();
                }
            }
            else
            {
                const auto hadHeldNote = noteStack_.current().active;
                const auto selection = noteStack_.noteOn(note, velocity);

                const auto strokeMode = static_cast<fiddle::BowStrokeMode>(
                    static_cast<int>(parameters_.getRawParameterValue("strokeMode")->load()));
                engine_.beginBowStroke(
                    fiddle::shouldAlternateBow(strokeMode, hadHeldNote));

                activePairLowerString_.store(
                    pairForMidiNote(selection.note), std::memory_order_relaxed);
                activeMidiNote_.store(selection.note, std::memory_order_relaxed);

                engine_.noteOn(midiNoteToHz(selection.note), selection.velocity);
                engine_.retune(bentFrequencyForNote(selection.note));
            }
        }
        else if (message.isNoteOff())
        {
            const auto note = message.getNoteNumber();

            if (playMode == static_cast<int>(fiddle::PlayMode::FiddlePlay))
            {
                if (fiddle::isBowActionKey(note))
                {
                    releaseFiddlePlayAction(note);
                }
                else if (fiddle::isFingeringKey(note))
                {
                    fingeringKeyDown_[static_cast<std::size_t>(note)] = false;
                    if (!fingeringHold_)
                    {
                        noteStack_.noteOff(note);
                        updateFiddlePlayFingering();
                    }
                }
            }
            else
            {
                const auto selection = noteStack_.noteOff(note);
                if (selection.changed)
                {
                    if (selection.active)
                    {
                        activePairLowerString_.store(
                            pairForMidiNote(selection.note), std::memory_order_relaxed);
                        activeMidiNote_.store(selection.note, std::memory_order_relaxed);
                        engine_.noteOn(midiNoteToHz(selection.note), selection.velocity);
                        engine_.retune(bentFrequencyForNote(selection.note));
                    }
                    else
                    {
                        activeMidiNote_.store(-1, std::memory_order_relaxed);
                        engine_.noteOff();
                    }
                }
            }
        }
        else if (message.isController() && message.getControllerNumber() == 64
                 && playMode == static_cast<int>(fiddle::PlayMode::FiddlePlay))
        {
            fingeringPedalHold_ = message.getControllerValue() >= 64;
            const bool parameterHold =
                parameters_.getRawParameterValue("fingeringHold")->load() >= 0.5f;
            updateFingeringHoldState(parameterHold || fingeringPedalHold_);
        }
        else if (message.isController() && message.getControllerNumber() == 1)
        {
            modWheelNormalized_ =
                static_cast<float>(message.getControllerValue()) / 127.0f;
            applyPerformanceControls();
        }
        else if (message.isChannelPressure())
        {
            channelPressureNormalized_ =
                static_cast<float>(message.getChannelPressureValue()) / 127.0f;
            applyPerformanceControls();
        }
        else if (message.isPitchWheel())
        {
            const auto value = message.getPitchWheelValue();
            pitchWheelNormalized_ = value >= 8192
                ? static_cast<float>(value - 8192) / 8191.0f
                : static_cast<float>(value - 8192) / 8192.0f;

            if (playMode == static_cast<int>(fiddle::PlayMode::FiddlePlay))
            {
                updateFiddlePlayFingering();
            }
            else
            {
                const auto selection = noteStack_.current();
                if (selection.active)
                    engine_.retune(bentFrequencyForNote(selection.note));
            }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            noteStack_.reset();
            resetPerformanceModeState();
        }
    }

    renderUntil(numBlockSamples);

    // Post-model listening level only. This never feeds back into bow/string/body
    // mechanics, so it can make the instrument DAW-friendly without changing tone.
    for (int sample = 0; sample < numBlockSamples; ++sample)
    {
        const auto gain = outputGainLinear_.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.getWritePointer(channel)[sample] *= gain;
    }

    const auto debug = engine_.debugSnapshot();

    if (playModeOneShotLatched_ && !debug.oneShotActive)
    {
        playModeOneShotLatched_ = false;
        playModePressureBoost_ = 0.0f;
        playModeSpeedScale_ = 1.0f;
        playModeResponseBoost_ = 0.0f;
        activeBowActionNote_ = -1;
        if (noteStack_.current().active)
            updateFiddlePlayFingering();
        visualBowAction_.store(
            static_cast<int>(fiddle::BowAction::None),
            std::memory_order_relaxed);
        applyPerformanceControls();
    }

    visualPrimaryString_.store(debug.primaryString, std::memory_order_relaxed);
    visualBowDirection_.store(debug.bowDirection, std::memory_order_relaxed);
    activePairLowerString_.store(debug.bowPairLowerString, std::memory_order_relaxed);

    for (std::size_t i = 0; i < debug.speakingFrequencyHz.size(); ++i)
    {
        visualSpeakingFrequencyHz_[i].store(
            debug.speakingFrequencyHz[i], std::memory_order_relaxed);
        visualContactTemperatureC_[i].store(
            debug.contactTemperatureC[i], std::memory_order_relaxed);
    }
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

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "playMode", "Play Mode",
        juce::StringArray { "Chromatic (keyboard)", "Fiddle Play (performance)" }, 1));

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
        "vibratoWidth", "Vibrato Width",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "vibratoPace", "Vibrato Pace",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "fingeringHold", "Fingering Hold", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "strokeMode", "Bow Strokes",
        juce::StringArray { "Fiddle Auto", "Connected", "Alternate" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "bodyMaterial", "Body Material",
        juce::StringArray {
            "Traditional spruce/maple",
            "Light stiff composite",
            "Dense experimental",
            "Rigid composite"
        }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "bowMaterial", "Bow Stick Material",
        juce::StringArray {
            "Pernambuco-like",
            "Carbon-like",
            "Light rigid experimental",
            "Flexible experimental"
        }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "contactMaterial", "Hair / Rosin",
        juce::StringArray {
            "Horsehair + medium rosin",
            "Dry / light grip",
            "High-grip rosin",
            "Synthetic hair"
        }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "stringMaterial", "String Core",
        juce::StringArray {
            "Synthetic core",
            "Steel core",
            "Gut-like"
        }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "bendRange", "Pitch Bend Range",
        juce::NormalisableRange<float>(1.0f, 24.0f, 1.0f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputLevelDb", "Output Level",
        juce::NormalisableRange<float>(-24.0f, 30.0f, 0.1f), 18.0f));

    return { params.begin(), params.end() };
}

float FiddleModelAudioProcessor::midiNoteToHz(int midiNote)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

int FiddleModelAudioProcessor::pairForMidiNote(int midiNote) noexcept
{
    if (midiNote < 62) return 0;
    if (midiNote < 69) return 1;
    return 2;
}

float FiddleModelAudioProcessor::bentFrequencyForNote(int midiNote) const
{
    const auto semitones =
        pitchWheelNormalized_ * pitchBendRangeSemitones_;
    return midiNoteToHz(midiNote)
        * std::pow(2.0f, semitones / 12.0f);
}

void FiddleModelAudioProcessor::updateFiddlePlayFingering()
{
    const auto current = noteStack_.current();

    if (!current.active)
    {
        std::array<float, 4> openStrings{};
        const bool bowActive =
            activeBowActionNote_ >= 0 || playModeOneShotLatched_;

        if (bowActive)
        {
            // In a monophonic keyboard phrase, a short gap between fingering
            // keys is not an instruction to bow the open string. Keep the last
            // stopped speaking length until the next fingering arrives. Open
            // strings remain explicitly playable via their G3/D4/A4/E5 keys.
            // This prevents an open string from becoming a dominant drone
            // behind the moving stopped pitch.
            if (playModeMonophonicPhrase_
                && playModePreferredPrimaryString_ >= 0)
            {
                activeMidiNote_.store(-1, std::memory_order_relaxed);
                visualFingeringMask_.store(0, std::memory_order_relaxed);
                return;
            }

            // A bow action may also be armed before the first fingering. Do not
            // invent a default D string in that state; wait for the left hand
            // to establish the physical string.
            if (playModeBowArmed_)
            {
                activeMidiNote_.store(-1, std::memory_order_relaxed);
                visualFingeringMask_.store(0, std::memory_order_relaxed);
                return;
            }

            const auto primary = std::clamp(
                playModePreferredPrimaryString_ >= 0
                    ? playModePreferredPrimaryString_
                    : 1,
                0, 3);
            auto pair = std::clamp(
                activePairLowerString_.load(std::memory_order_relaxed),
                0, 2);
            if (primary < pair || primary > pair + 1)
                pair = std::clamp(primary, 0, 2);

            engine_.setFingeringLayout(
                openStrings, primary, pair, 0.8f);
            playModeAutoFocusEnabled_ = false;
            playModeAutoFocusValue_ = 0.0f;
            applyPerformanceControls();
            activeMidiNote_.store(-1, std::memory_order_relaxed);
            activePairLowerString_.store(pair, std::memory_order_relaxed);
            visualFingeringMask_.store(0, std::memory_order_relaxed);
            return;
        }

        engine_.setFingeringLayout(openStrings, 1, 1, 0.8f);
        engine_.stopBow();
        playModePreferredPrimaryString_ = -1;
        playModeAutoFocusEnabled_ = false;
        playModeAutoFocusValue_ = 0.0f;
        activeMidiNote_.store(-1, std::memory_order_relaxed);
        activePairLowerString_.store(1, std::memory_order_relaxed);
        visualFingeringMask_.store(0, std::memory_order_relaxed);
        return;
    }

    std::array<int, 4> heldNotes { -1, -1, -1, -1 };
    std::size_t count = 0;

    heldNotes[count++] = current.note;

    for (int note = fiddle::fiddleLowestNote;
         note <= fiddle::fiddleHighestNote && count < heldNotes.size();
         ++note)
    {
        if (note != current.note && noteStack_.isHeld(note))
            heldNotes[count++] = note;
    }

    const bool bowActive =
        activeBowActionNote_ >= 0 || playModeOneShotLatched_;

    if (!bowActive)
    {
        // Shape prepared before bowing decides whether this stroke is a
        // melodic one-string phrase or an explicit polyphonic/double-stop
        // shape. This makes ordinary keyboard note overlap usable for slurs.
        playModeMonophonicPhrase_ = count == 1;
    }
    else if (playModePreferredPrimaryString_ < 0 && count == 1)
    {
        // Bow-first workflow: C2 may be held before the first fingering key.
        // With no pre-bowed chord shape, treat the arriving fingering as a
        // monophonic phrase so subsequent key overlap remains a slur.
        playModeMonophonicPhrase_ = true;
    }
    count = fiddle::collapseMelodicBowOverlap(
        heldNotes,
        count,
        current.note,
        bowActive,
        playModeMonophonicPhrase_,
        fingeringHold_);

    std::uint64_t fingeringMask = 0;
    for (std::size_t i = 0; i < count; ++i)
        fingeringMask |= fiddle::fingeringMaskBit(heldNotes[i]);
    visualFingeringMask_.store(
        fingeringMask, std::memory_order_relaxed);

    const auto layout = fiddle::voiceFingering(
        heldNotes, count, current.note, playModePreferredPrimaryString_);

    playModeAutoFocusEnabled_ = count == 1;
    playModeAutoFocusValue_ =
        fiddle::singleStringFocusForLayout(layout, count);

    std::array<float, 4> frequencies{};
    for (std::size_t stringIndex = 0; stringIndex < frequencies.size(); ++stringIndex)
    {
        const auto note = layout.midiNoteByString[stringIndex];
        if (note >= 0)
            frequencies[stringIndex] = bentFrequencyForNote(note);
    }

    engine_.setFingeringLayout(
        frequencies,
        layout.primaryString,
        layout.bowPairLowerString,
        0.85f);

    // Fingering can change which physical string is primary while a bow
    // action is already held. Re-apply performance controls here so the
    // newly computed monophonic auto-focus reaches the engine immediately.
    // Without this, the UI/speaking length moves but a C2-first workflow can
    // leave the bow centred across the old pair, letting the neighbouring
    // open string dominate the audible pitch.
    applyPerformanceControls();

    if (playModeBowArmed_)
    {
        const auto armedAction =
            fiddle::bowActionForMidiNote(activeBowActionNote_);
        switch (armedAction)
        {
            case fiddle::BowAction::DownBow:
                playBowDirection_ = 1;
                engine_.startBow(1);
                break;
            case fiddle::BowAction::UpBow:
                playBowDirection_ = -1;
                engine_.startBow(-1);
                break;
            case fiddle::BowAction::Tremolo:
            {
                const auto gesture = fiddle::makeBowGestureProfile(
                    armedAction, playModeGestureStrength_);
                engine_.startTremolo(gesture.tremoloReversalsPerSecond);
                break;
            }
            case fiddle::BowAction::Shuffle:
            {
                const auto gesture = fiddle::makeBowGestureProfile(
                    armedAction, playModeGestureStrength_);
                engine_.startShuffle(gesture.shuffleSubdivisionsPerSecond);
                break;
            }
            default:
                break;
        }
        playModeBowArmed_ = false;
    }

    playModePreferredPrimaryString_ = layout.primaryString;
    activeMidiNote_.store(current.note, std::memory_order_relaxed);
    activePairLowerString_.store(
        layout.bowPairLowerString, std::memory_order_relaxed);
}

void FiddleModelAudioProcessor::triggerFiddlePlayAction(int midiNote, float velocity)
{
    const auto action = fiddle::bowActionForMidiNote(midiNote);
    visualBowAction_.store(static_cast<int>(action), std::memory_order_relaxed);

    if (action == fiddle::BowAction::Release)
    {
        engine_.stopBow();
        activeBowActionNote_ = -1;
        playModeOneShotLatched_ = false;
        playModeMonophonicPhrase_ = false;
        playModeFocusOverride_ = false;
        playModePressureBoost_ = 0.0f;
        playModeSpeedScale_ = 1.0f;
        playModeResponseBoost_ = 0.0f;
        applyPerformanceControls();
        return;
    }

    const bool hasFingering = noteStack_.current().active;
    if (!hasFingering
        && action != fiddle::BowAction::DroneBow)
    {
        // Bow-first workflow: arm the right hand but do not excite an
        // arbitrary default string. The first G3+ fingering selects the
        // physical string and starts the already-held continuous bow action.
        playModeMonophonicPhrase_ = true;
        playModePreferredPrimaryString_ = -1;
        playModeAutoFocusEnabled_ = false;
        playModeAutoFocusValue_ = 0.0f;
    }

    const auto gestureStrength = std::clamp(velocity, 0.0f, 1.0f);
    playModeGestureStrength_ = gestureStrength;
    const auto gesture = fiddle::makeBowGestureProfile(action, gestureStrength);
    playModeOneShotLatched_ = false;
    playModeResponseBoost_ = gesture.responseBoost;
    engine_.setStrokeBite(
        gesture.biteBoost, gesture.biteDurationSeconds);

    switch (action)
    {
        case fiddle::BowAction::DownBow:
            playModeSpeedScale_ = gesture.speedScale;
            playModeFocusOverride_ = false;
            playModePressureBoost_ = gesture.pressureBoost;
            applyPerformanceControls();
            playBowDirection_ = 1;
            activeBowActionNote_ = midiNote;
            if (hasFingering)
                engine_.startBow(1);
            else
                playModeBowArmed_ = true;
            break;

        case fiddle::BowAction::UpBow:
            playModeSpeedScale_ = gesture.speedScale;
            playModeFocusOverride_ = false;
            playModePressureBoost_ = gesture.pressureBoost;
            applyPerformanceControls();
            playBowDirection_ = -1;
            activeBowActionNote_ = midiNote;
            if (hasFingering)
                engine_.startBow(-1);
            else
                playModeBowArmed_ = true;
            break;

        case fiddle::BowAction::ShortStroke:
            playModeSpeedScale_ = gesture.speedScale;
            playModeFocusOverride_ = false;
            playModePressureBoost_ = gesture.pressureBoost;
            applyPerformanceControls();
            playBowDirection_ = -playBowDirection_;
            engine_.startShortStroke(
                playBowDirection_,
                gesture.durationSeconds,
                gesture.liftDurationSeconds,
                gesture.liftBrake,
                gesture.liftForceCurve);
            playModeOneShotLatched_ = true;
            activeBowActionNote_ = -1;
            break;

        case fiddle::BowAction::Tremolo:
            playModeSpeedScale_ = gesture.speedScale;
            playModeFocusOverride_ = false;
            playModePressureBoost_ = gesture.pressureBoost;
            applyPerformanceControls();
            activeBowActionNote_ = midiNote;
            if (hasFingering)
                engine_.startTremolo(gesture.tremoloReversalsPerSecond);
            else
                playModeBowArmed_ = true;
            break;

        case fiddle::BowAction::Shuffle:
            playModeSpeedScale_ = gesture.speedScale;
            playModeFocusOverride_ = false;
            playModePressureBoost_ = gesture.pressureBoost;
            applyPerformanceControls();
            activeBowActionNote_ = midiNote;
            if (hasFingering)
                engine_.startShuffle(gesture.shuffleSubdivisionsPerSecond);
            else
                playModeBowArmed_ = true;
            break;

        case fiddle::BowAction::DroneBow:
            playModeSpeedScale_ = gesture.speedScale;
            playModeFocusOverride_ = true;
            playModeFocusValue_ = 0.0f;
            playModePressureBoost_ = gesture.pressureBoost;
            applyPerformanceControls();
            engine_.startBow(playBowDirection_);
            activeBowActionNote_ = midiNote;
            break;

        case fiddle::BowAction::AccentStroke:
            playModeSpeedScale_ = gesture.speedScale;
            playModeFocusOverride_ = false;
            playModePressureBoost_ = gesture.pressureBoost;
            applyPerformanceControls();
            playBowDirection_ = -playBowDirection_;
            engine_.startShortStroke(
                playBowDirection_,
                gesture.durationSeconds,
                gesture.liftDurationSeconds,
                gesture.liftBrake,
                gesture.liftForceCurve);
            playModeOneShotLatched_ = true;
            activeBowActionNote_ = -1;
            break;

        case fiddle::BowAction::Chop:
            // Physical chop: low bow travel/high normal force plus a short
            // transverse collision injected at the bowing point.
            playModeFocusOverride_ = false;
            playModePressureBoost_ = gesture.pressureBoost;
            playModeSpeedScale_ = gesture.speedScale;
            applyPerformanceControls();
            playBowDirection_ = -playBowDirection_;
            engine_.startChop(
                playBowDirection_,
                gesture.durationSeconds,
                gesture.impactVelocityMps,
                gesture.impactDurationSeconds);
            playModeOneShotLatched_ = true;
            activeBowActionNote_ = -1;
            break;

        case fiddle::BowAction::None:
        case fiddle::BowAction::Release:
            break;
    }
}

void FiddleModelAudioProcessor::releaseFiddlePlayAction(int midiNote)
{
    const auto action = fiddle::bowActionForMidiNote(midiNote);

    if (action == fiddle::BowAction::Release)
    {
        visualBowAction_.store(
            static_cast<int>(fiddle::BowAction::None),
            std::memory_order_relaxed);
        return;
    }

    if (action == fiddle::BowAction::ShortStroke
        || action == fiddle::BowAction::AccentStroke
        || action == fiddle::BowAction::Chop)
    {
        // One-shot gestures own their complete physical lifetime. Releasing the
        // controller key must not change pressure/speed/response halfway through
        // the stroke; the end-of-block engine state clears these after bow lift.
        return;
    }

    if (activeBowActionNote_ != midiNote)
        return;

    if (action == fiddle::BowAction::DownBow
        || action == fiddle::BowAction::UpBow
        || action == fiddle::BowAction::Tremolo
        || action == fiddle::BowAction::Shuffle
        || action == fiddle::BowAction::DroneBow)
    {
        engine_.stopBow();
    }

    if (action == fiddle::BowAction::DroneBow)
        playModeFocusOverride_ = false;

    playModePressureBoost_ = 0.0f;
    playModeSpeedScale_ = 1.0f;
    playModeResponseBoost_ = 0.0f;
    playModeBowArmed_ = false;
    applyPerformanceControls();
    activeBowActionNote_ = -1;

    // Once the bow is released, expose the physically held shape again so a
    // player can prepare an intentional double stop before the next stroke.
    if (noteStack_.current().active)
        updateFiddlePlayFingering();
    visualBowAction_.store(
        static_cast<int>(fiddle::BowAction::None),
        std::memory_order_relaxed);
}

void FiddleModelAudioProcessor::resetPerformanceModeState() noexcept
{
    engine_.stopBow();

    activeBowActionNote_ = -1;
    playBowDirection_ = 1;
    playModeFocusOverride_ = false;
    playModeAutoFocusEnabled_ = false;
    fingeringHold_ = false;
    fingeringPedalHold_ = false;
    fingeringKeyDown_.fill(false);
    playModeFocusValue_ = 0.0f;
    playModeAutoFocusValue_ = 0.0f;
    playModePressureBoost_ = 0.0f;
    playModeSpeedScale_ = 1.0f;
    playModeResponseBoost_ = 0.0f;
    playModeOneShotLatched_ = false;
    playModeBowArmed_ = false;
    playModeMonophonicPhrase_ = false;
    playModeGestureStrength_ = 0.5f;
    playModePreferredPrimaryString_ = -1;

    activeMidiNote_.store(-1, std::memory_order_relaxed);
    activePairLowerString_.store(1, std::memory_order_relaxed);
    visualBowAction_.store(
        static_cast<int>(fiddle::BowAction::None),
        std::memory_order_relaxed);
    visualEffectiveStringFocus_.store(0.0f, std::memory_order_relaxed);
    visualFingeringHold_.store(false, std::memory_order_relaxed);
    visualFingeringMask_.store(0, std::memory_order_relaxed);

    std::array<float, 4> openStrings{};
    engine_.setFingeringLayout(openStrings, 1, 1, 0.8f);
    applyPerformanceControls();
}

void FiddleModelAudioProcessor::updateFingeringHoldState(bool enabled)
{
    if (enabled == fingeringHold_)
        return;

    fingeringHold_ = enabled;
    visualFingeringHold_.store(
        fingeringHold_, std::memory_order_relaxed);

    if (!fingeringHold_)
    {
        for (int note = fiddle::fiddleLowestNote;
             note <= fiddle::fiddleHighestNote; ++note)
        {
            if (!fingeringKeyDown_[static_cast<std::size_t>(note)]
                && noteStack_.isHeld(note))
            {
                noteStack_.noteOff(note);
            }
        }
        updateFiddlePlayFingering();
    }
}

FiddleVisualState FiddleModelAudioProcessor::visualState() const noexcept
{
    FiddleVisualState state;
    state.midiNote = activeMidiNote_.load(std::memory_order_relaxed);
    state.active = state.midiNote >= 0;
    state.primaryString = visualPrimaryString_.load(std::memory_order_relaxed);
    state.pairLowerString = activePairLowerString_.load(std::memory_order_relaxed);
    state.bowDirection = visualBowDirection_.load(std::memory_order_relaxed);
    state.playMode = visualPlayMode_.load(std::memory_order_relaxed);
    state.bowAction = visualBowAction_.load(std::memory_order_relaxed);
    state.fingeringHold =
        visualFingeringHold_.load(std::memory_order_relaxed);
    state.fingeringMask =
        visualFingeringMask_.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < state.speakingFrequencyHz.size(); ++i)
    {
        state.speakingFrequencyHz[i] =
            visualSpeakingFrequencyHz_[i].load(std::memory_order_relaxed);
        state.contactTemperatureC[i] =
            visualContactTemperatureC_[i].load(std::memory_order_relaxed);
    }

    state.bowContact = parameters_.getRawParameterValue("position")->load();
    state.stringFocus =
        visualEffectiveStringFocus_.load(std::memory_order_relaxed);
    return state;
}

void FiddleModelAudioProcessor::applyPerformanceControls() noexcept
{
    auto controls = baseControls_;
    controls.singleStringIsolation = 0.0f;

    controls.pressure = std::clamp(
        controls.pressure
            + 0.30f * channelPressureNormalized_
            + playModePressureBoost_,
        0.0f, 1.0f);

    controls.vibratoWidth = std::max(
        controls.vibratoWidth, modWheelNormalized_);
    controls.speed = std::clamp(
        controls.speed * playModeSpeedScale_, 0.0f, 1.0f);
    controls.attack = std::clamp(
        controls.attack + playModeResponseBoost_, 0.0f, 1.0f);

    if (playModeFocusOverride_)
    {
        controls.balance = playModeFocusValue_;
    }
    else if (playModeAutoFocusEnabled_
             && std::abs(controls.balance) < 0.08f)
    {
        // A centred/default String Focus means "follow the primary string" in
        // monophonic Fiddle Play. Moving the knob away from centre remains an
        // explicit manual override. Drone Bow uses the stronger gesture
        // override above and therefore still centres the bow across the pair.
        controls.balance = playModeAutoFocusValue_;
        controls.singleStringIsolation = 1.0f;
    }

    visualEffectiveStringFocus_.store(
        controls.balance, std::memory_order_relaxed);
    engine_.setControls(controls);
}

void FiddleModelAudioProcessor::requestPlayActionFromUi(
    int midiNote, bool pressed) noexcept
{
    if (!fiddle::isBowActionKey(midiNote))
        return;

    if (pressed)
        pendingUiActionPress_.store(midiNote, std::memory_order_relaxed);
    else
        pendingUiActionRelease_.store(midiNote, std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FiddleModelAudioProcessor();
}
