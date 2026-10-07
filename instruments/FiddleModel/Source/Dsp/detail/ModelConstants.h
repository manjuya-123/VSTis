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

// Reduced finite-width bow contact. A violin bow does not constrain one
// mathematical point of the string: roughly a centimetre of hair ribbon
// samples a small span of the travelling wave. The current waveguide still
// has one force junction, so use a conservative three-point spatial average
// (centre + two virtual edges) only to determine the nonlinear contact force.
// This is inside the bow/string mechanics, not an output low-pass or EQ.
inline constexpr double violinSpeakingLengthMeters = 0.328;
inline constexpr double bowHairContactWidthMeters = 0.010;
// Preserve a meaningful finite-width effect; boundary handling in the
// waveguide limits the virtual half-width symmetrically when a contact edge
// would fall inside the sub-sample bridge/nut region.
inline constexpr double finiteWidthContactBlend = 0.35;
// Thin A/E strings expose the point-contact approximation most clearly.
// Increase only their finite-ribbon contribution while leaving the calibrated
// G/D contact strength unchanged. The nonlinear force remains a single shared
// physical contact; this only changes how much spatial wave information the
// contact law observes.
inline constexpr std::array<double, stringCount> finiteWidthStringScale {
    1.00, 1.00, 1.00, 1.00
};

// Reduced distributed-hair contact. The existing finite-width read already
// samples the travelling wave across the ribbon, but one BowContact state made
// every hair section stick and slip in lockstep. Keep the calibrated single
// contact on G/D; on the thinner A/E strings, blend a small contribution from
// independent bridgeward/nutward contact states. These are quadrature samples
// of one physical ribbon, so their forces are averaged rather than summed.
inline constexpr std::array<double, stringCount> distributedHairContactBlend {
    0.0, 0.0, 0.08, 0.12
};

// Weak feedback depth for the reduced torsional/contact surface-velocity state.
// The lower strings already show contact irregularity, while A is strongly
// cycle-locked; keep every value conservative so this perturbs Helmholtz timing
// without creating a second audible pitch source.
inline constexpr std::array<double, stringCount> torsionalContactCoupling {
    0.0, 0.0, 0.0, 0.0
};

// Reduced torsional propagation. These states never mix directly into audio or
// bridge translation; they only perturb the velocity seen by BowContact.
// Torsional waves travel several times faster than transverse waves and lose
// substantial energy at both terminations, preserving short memory without a
// second sustained audible pitch.
inline constexpr std::array<double, stringCount> torsionalSpeedRatio {
    5.35, 5.10, 4.85, 4.60
};
inline constexpr std::array<double, stringCount> torsionalBridgeReflection {
    -0.58, -0.57, -0.56, -0.55
};
inline constexpr std::array<double, stringCount> torsionalNutReflection {
    -0.76, -0.75, -0.74, -0.73
};
inline constexpr std::array<double, stringCount> torsionalDriveVelocityPerNewton {
    0.0032, 0.0036, 0.0041, 0.0046
};
inline constexpr std::array<double, stringCount> torsionalFeedbackScale {
    0.0, 0.0, 0.0, 0.0
};

// Delayed torsion can alter the local friction reserve without being summed as
// a sustained transverse velocity. Start with A only, where the baseline bow
// contact is uniquely cycle-locked. The normalized perturbation is deliberately
// only a few percent so it moves slip timing rather than retuning the string.
inline constexpr std::array<double, stringCount> torsionalGripModulationDepth {
    0.0, 0.0, 0.024, 0.0
};

// Mesoscopic horsehair/rosin contact-patch variation. Open and stopped strings
// use separate conservative depths because fingertip termination loss changes
// how much friction-threshold variation can be tolerated without masking the
// low-order pitch harmonics.
inline constexpr std::array<double, stringCount> contactPatchGripDepthOpen {
    0.0, 0.0, 0.0320, 0.0100
};
inline constexpr std::array<double, stringCount> contactPatchGripDepthStopped {
    0.0, 0.0, 0.0040, 0.0050
};

// The short residual velocity burst used to make rosin catch/release audible
// is not the nonlinear friction solve itself. On the low-impedance E string,
// the same absolute added velocity becomes disproportionately prominent and
// can sound like a pitch-synchronous "tick". Keep the physical stick/slip
// transition intact, but scale only this residual surface-texture velocity.
inline constexpr std::array<double, stringCount> rosinTransitionVelocityScale {
    1.00, 1.00, 1.00, 1.00
};

// Fingered notes terminate against a soft fingertip rather than the hard nut.
// Apply additional frequency-dependent reflection loss only on the thin E
// string, where stopped notes currently retain a very sharp Helmholtz corner.
// Open E and all G/D/A notes remain on the existing calibrated path.
inline constexpr std::array<double, stringCount> fingerReflectionAlpha {
    0.00, 0.00, 0.00, 0.45
};

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
    0.018, 0.022, 0.012, 0.016
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
