#pragma once

#include <array>

namespace fiddle::detail
{
inline constexpr int stringCount = 4;
inline constexpr int bodyModeCount = 12;
inline constexpr int delaySize = 4096;
inline constexpr double pi = 3.1415926535897932384626433832795;
inline constexpr double balanceSharpness = 1.75;

inline constexpr std::array<double, stringCount> openFrequency {
    195.9977, 293.6648, 440.0, 659.2551
};

// N s/m. Derived from the prototype scale length/tensions used in the research model.
inline constexpr std::array<double, stringCount> stringImpedance {
    0.37332444057923675,
    0.23878228245098557,
    0.19054878048780488,
    0.13873751546881677
};

inline constexpr std::array<double, stringCount> lossGain {
    0.9988, 0.9990, 0.9992, 0.99935
};

inline constexpr std::array<double, stringCount> lossAlpha {
    0.018, 0.015, 0.012, 0.010
};

inline constexpr std::array<double, stringCount> allpassA {
    -0.055, -0.040, -0.025, -0.015
};

struct BodyModeDefinition
{
    double frequencyHz;
    double zeta;
    double peakAdmittance;
};

// Representative violin regions, not a fit to one specific instrument.
inline constexpr std::array<BodyModeDefinition, bodyModeCount> bodyModes {{
    { 280.0,  0.070, 0.0028 },
    { 405.0,  0.065, 0.0015 },
    { 465.0,  0.050, 0.0045 },
    { 550.0,  0.045, 0.0060 },
    { 720.0,  0.055, 0.0030 },
    { 920.0,  0.065, 0.0027 },
    { 1180.0, 0.075, 0.0028 },
    { 1500.0, 0.090, 0.0032 },
    { 1900.0, 0.110, 0.0038 },
    { 2350.0, 0.120, 0.0065 },
    { 2850.0, 0.140, 0.0050 },
    { 3500.0, 0.180, 0.0034 },
}};

inline constexpr double bodyDirectConductance = 0.0007;
} // namespace fiddle::detail
