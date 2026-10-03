#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace fiddle::presentation
{
inline juce::String bowPressure(float value)
{
    if (value < 0.15f) return "Feather";
    if (value < 0.35f) return "Light";
    if (value < 0.65f) return "Natural";
    if (value < 0.85f) return "Firm";
    return "Heavy";
}

inline juce::String bowSpeed(float value)
{
    if (value < 0.15f) return "Very slow";
    if (value < 0.35f) return "Measured";
    if (value < 0.65f) return "Natural";
    if (value < 0.85f) return "Fast";
    return "Very fast";
}

inline juce::String bowResponse(float value)
{
    if (value < 0.15f) return "Soft";
    if (value < 0.35f) return "Smooth";
    if (value < 0.65f) return "Natural";
    if (value < 0.85f) return "Quick";
    return "Crisp";
}

inline juce::String bowContact(float value)
{
    if (value < 0.15f) return "Over fingerboard";
    if (value < 0.35f) return "Warm side";
    if (value < 0.65f) return "Middle";
    if (value < 0.85f) return "Bridge side";
    return "Near bridge";
}

inline juce::String stringFocus(float value)
{
    if (value < -0.65f) return "Lower string";
    if (value < -0.20f) return "Lower-biased";
    if (value <= 0.20f) return "Even pair";
    if (value <= 0.65f) return "Upper-biased";
    return "Upper string";
}

inline juce::String bendRange(float value)
{
    const auto semitones = static_cast<int>(std::lround(value));
    return juce::String::fromUTF8(u8"\u00b1") + juce::String(semitones)
         + (semitones == 1 ? " semitone" : " semitones");
}
} // namespace fiddle::presentation
