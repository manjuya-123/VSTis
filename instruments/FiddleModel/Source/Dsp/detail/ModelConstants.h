#pragma once

#include <array>

namespace fiddle::detail
{
inline constexpr int stringCount = 4;
inline constexpr int bodyModeCount = 20;
inline constexpr int delaySize = 4096;
inline constexpr double pi = 3.1415926535897932384626433832795;
inline constexpr double balanceSharpness = 1.75;

// Normalized bow position beta = distance from bridge / speaking length.
// These are player-facing endpoints, not a claim about one specific violin.
inline constexpr double bowBetaFingerboard = 0.25;
inline constexpr double bowBetaBridge = 0.04;

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
    -0.055, -0.0325, -0.025, -0.015
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

    // Above the strongest bridge/body regions, a real fiddle does not become
    // spectrally empty. Use a sparse, increasingly damped modal tail rather
    // than one bright shelf; these remain part of the mechanical admittance.
    { 4050.0, 0.200, 0.00135 },
    { 4550.0, 0.215, 0.00120 },
    { 5100.0, 0.230, 0.00110 },
    { 5700.0, 0.245, 0.00100 },
    { 6350.0, 0.265, 0.00088 },
    { 7050.0, 0.285, 0.00076 },
    { 7800.0, 0.310, 0.00062 },
    { 8650.0, 0.340, 0.00048 },
}};


// Bridge-rocking/lateral mobility is not just the vertical body spectrum shifted
// wholesale. Keep the same modal count for a cheap real-time solve, but use a
// distinct, more irregular distribution with relatively dense upper modes.
// These are representative regions rather than a fit to one specific violin.
inline constexpr std::array<BodyModeDefinition, bodyModeCount> rockingBodyModes {{
    { 315.0,  0.080, 0.0020 },
    { 390.0,  0.072, 0.0018 },
    { 515.0,  0.060, 0.0038 },
    { 625.0,  0.058, 0.0046 },
    { 805.0,  0.068, 0.0033 },
    { 1035.0, 0.078, 0.0032 },
    { 1325.0, 0.090, 0.0034 },
    { 1660.0, 0.105, 0.0038 },
    { 2075.0, 0.120, 0.0044 },
    { 2525.0, 0.135, 0.0050 },
    { 3075.0, 0.155, 0.0044 },
    { 3790.0, 0.185, 0.0036 },
    { 4310.0, 0.205, 0.00155 },
    { 4860.0, 0.220, 0.00138 },
    { 5480.0, 0.238, 0.00120 },
    { 6140.0, 0.255, 0.00105 },
    { 6820.0, 0.278, 0.00090 },
    { 7560.0, 0.300, 0.00076 },
    { 8380.0, 0.325, 0.00062 },
    { 9250.0, 0.355, 0.00048 },
}};

inline constexpr double bodyDirectConductance = 0.0007;

// Representative bridge cross-section used by the v0.4b/v0.4c geometry model.
inline constexpr double bridgeRadiusMm = 41.0;
inline constexpr double geSpacingMm = 34.925;
inline constexpr double adjacentSpacingMm = geSpacingMm / 3.0;
inline constexpr std::array<double, stringCount> bridgeXmm {
    -17.4625, -5.820833333333333, 5.820833333333333, 17.4625
};
inline constexpr std::array<double, stringCount> bridgeYmm {
    0.0, 3.4894028492500837, 3.4894028492500837, 0.0
};
inline constexpr std::array<double, 3> pairChordAngleDeg {
    16.685258826933733, 0.0, -16.685258826933733
};
inline constexpr double contactStiffness = 0.30;
inline constexpr double contactExponent = 1.5;
} // namespace fiddle::detail
