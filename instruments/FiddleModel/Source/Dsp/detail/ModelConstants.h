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

// Bow friction acts on the string surface, so transverse translation and
// torsional rotation contribute to the same relative bow/string velocity.
// A published bowed-string reference case uses torsional wave speed about
// 5.2 times the transverse speed and a surface-referred torsional impedance
// of 1.8 N s/m. Use that absolute impedance as a deliberately conservative
// first integration rather than scaling it down with each violin string's
// transverse impedance; the earlier ratio-scaled experiment over-coupled the
// thin E string and made the model sample-rate sensitive.
inline constexpr double torsionalWaveSpeedRatio = 5.2;
inline constexpr double torsionalSurfaceImpedance = 1.8;
// This reduced model has one lumped torsional surface coordinate while a real
// finite-width bow excites a distributed ribbon/string contact. Couple only
// part of the lumped coordinate back into the point-contact solve so the
// reduced mode cannot stand in for the entire distributed torsional field.
// The same coefficient is used reciprocally for force -> torsion and torsion
// -> contact velocity, preserving a passive generalized coupling.
// Keep the first torsional integration deliberately secondary. At 0.10-0.15
// the nonlinear contact could fall into alternate attractors for otherwise
// identical A-string GUI/MIDI starts. A smaller reciprocal coupling still
// converts transverse bow work into the heavily damped torsional wave family
// without letting this single lumped torsional coordinate dominate the contact.
inline constexpr double torsionalReducedOrderCoupling = 0.05;

// Approximate constant-Q torsional loss. Q~45 corresponds to a complete-cycle
// amplitude retention exp(-pi/Q); split equally between the two ends.
inline constexpr double torsionalEndReflectionGain = 0.9657;

inline constexpr std::array<double, stringCount> lossGain {
    0.9988, 0.9990, 0.9992, 0.99935
};

// Passive, frequency-dependent reflection losses at the finger/string
// termination. The original very small coefficients preserved excessively
// sharp high-partial wavefronts through many round trips: the moving pitch
// was correct, but steady bowed tones retained a buzzy ideal-waveguide edge.
// Preserve G/A/E damping exactly, because the actual C2-held MIDI G
// sequence lost low-note presence when G damping was increased. Apply a
// minimal D-string-only change. These losses remain inside each
// string's feedback path, not as an EQ on the final radiated audio. The
// existing reflectionPhaseDelaySamples() calculation compensates the added
// low-frequency filter phase when setting the speaking length.
inline constexpr std::array<double, stringCount> lossAlpha {
    0.018, 0.022, 0.012, 0.010
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
    // Low body/air-region mobility must carry enough of the first few string
    // harmonics to preserve pitch identity. The earlier sparse bank gave the
    // 1.5-3 kHz modes too much perceptual authority: low strings contained the
    // correct fundamental, but it sat behind a nearly fixed body-formant sound.
    // Broaden and strengthen only the low modes; the bridge-hill/tail above
    // 1 kHz is deliberately left unchanged.
    { 280.0,  0.110, 0.00412 },
    { 405.0,  0.085, 0.00235 },
    { 465.0,  0.065, 0.00530 },
    { 550.0,  0.060, 0.00648 },
    { 720.0,  0.075, 0.00412 },
    { 920.0,  0.085, 0.00392 },
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

    // Broad residual low-frequency mobility. These heavily damped modes fill
    // the gaps below A0/B1 without creating another narrow body note, so low
    // strings can project their moving fundamental/low partials through the
    // body instead of being heard mainly through the fixed bridge-hill formant.
    { 235.0, 0.200, 0.00090 },
    { 340.0, 0.160, 0.00075 },
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

// Broadband mechanical mobility between the sparse resonant peaks. Keeping
// this too small makes the fixed body modes dominate low-string perception,
// so the moving stopped-string harmonic series can sound like a faint tone
// behind a stationary synthetic resonance. This remains inside the mechanical
// bridge/body admittance; it is not an output EQ or added oscillator.
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
