# A/E crossing rebuild: numerical and audible acceptance record

**Status: NOT READY.** The new model is a physically coupled two-string
prototype, not a validated fiddle tone or a replacement VST3. Existing
`FiddleEngine` and Play Mode have deliberately not been replaced.

## Instrument-neutral mechanical progress

- `shared/physical/MovingEndString1D.h`: dynamic boundary displacements
  and implicit-midpoint transverse string motion.
- `shared/physical/SharedBridgePair.h`: two strings attached to exactly
  one common bridge coordinate (baseline).
- `shared/physical/TwoPortBridgePair.h`: *distinct* A/E bridge contact
  coordinates linked by a positive, passive elastic rocker and damper.
- `VSTisSharedBridgeNumerics`, `VSTisTwoPortBridgeNumerics`: bridge
  excitation transfers vibrational energy across the strings and the
  total contact work equals change of string+bridge+spring energy and
  passive losses to numerical precision at 44.1 and 48 kHz.
- `instruments/FiddleModel/Source/Rebuild/TwoStringFiddleBridge.h`:
  continuous bow-force distribution between two strings, coupled
  implicit LuGre bristle contacts and simultaneous two-string dynamics.
  No note re-trigger or cleared wave state at the crossing.

## Why energy-pass / audible-fail must be separated

A 3-second A->E->A exercise is rendered with sustained bow speed and a
0.25-second continuous contact-force crossing in each direction.

The *old* common-coordinate bridge, in the E stage 1.2-1.7 s:
- A440 mechanical bridge lock-in amplitude: about 0.003023.
- E659 mechanical bridge lock-in amplitude: about 0.00000898.
- The requested E note is virtually absent. The solver still
  passes its energy ledger and stick/slide convergence tests.

The *new* two-port bridge, same E stage and inputs:
- A440 mechanical bridge amplitude: about 0.0001466.
- E659 mechanical bridge amplitude: about 0.0002059.
- This is genuine numerical improvement in E-string excitation.
  **However, after returning to A, the lingering E659 component remains
  stronger than A440 in the final A stage.** The model is NOT accepted.

Other important experiment:
- A uniform viscous string-loss estimate equivalent to an open violin
  D-string measured decay time of about 0.448 s (`loss=4.5/s`) was tried.
  It suppressed both carryover and essential sustained bow vibration,
  and was therefore *reverted*, not declared a fix.
- The body/radiation damping path and multi-mode instrument impedance
  require their own model. Uniform bulk-string damping cannot replace
  the actual energy loss through the corpus.

## New hard gating

`FiddleTwoStringCrossing`: mechanical stability, full energy ledger,
non-negative hair/rosin dissipation and continuous force roots.

`FiddleTwoStringCrossingToneGate`: independent, intentionally red while
the requested note is not dominant for **all three** A/E/A stages.
This gate compares extracted fundamentals, not simply RMS, absence of
NaNs, or an arbitrary output limiter. It is still *not* sufficient to
prove natural fiddle tone; human listening remains a separate gate.

CI uploads diagnostic WAVs even on this tonal failure. The audio
artifacts are labeled `UNVERIFIED` and use bridge velocity only:
they are not violin recordings or physical radiated audio.

## Still missing

Two orthogonal string polarisations, transverse/torsional coupling on E,
bow-hair ribbon width and longitudinal dynamics, actual rosin/history
calibration, passive bridge-to-body multi-mode admittance, source-filter
radiation, fingering geometry validation, 4-string performance and
Hillwind Reel MIDI/VST3 integration. Source code remains under
`work/fiddle-physical-rebuild` and PR #2 must remain a draft.

References for later modeling (not a claim these were implemented):
- Woodhouse, bridge modeling: https://euphonics.org/7-5-1-modelling-the-violin-bridge/
- Measured decay constants, `open D tau_1 = 0.448s`:
  https://dael.euracoustics.org/bin/EAA/aaua_dl?document_id=64660
- Real E-string torsional whistling:
  https://euphonics.org/9-5-getting-that-perfect-start-guettlers-diagram/

## Reverse crossing diagnostic and modal corpus rebuild

A->E->A on the two-port bridge was further decomposed into true actuator
work and the vibrational displacement of both strings:

- Returned A period 2.25-2.75 s: A-bow RMS traction 0.103 N;
  **E-bow RMS traction exactly zero**.
- Despite this, the returning A-string bow-contact 440-Hz velocity
  amplitude is about 0.042 m/s, while the unbowed E string has a 659-Hz
  bow-contact velocity amplitude about 0.233 m/s.
- Independent A-only and E-only steady tests produce their proper notes.
  Therefore the wrong-pitch symptom is *not* simply incorrect MIDI-to-string
  routing or a second note being re-triggered. The old string retains
  stored vibration and the body termination transfers/dissipates too little
  energy in that state.

A controlled passive grounded dashpot sweep (5, 15, 30, 60, 120, 240
N s/m) reduced the E-string residual but **every candidate destroyed
or drastically weakened E's bowed fundamental**. None passed the
bidirectional A/E/A pitch gate. No bulk-string-damping or high-pass EQ
"fix" is accepted.

The reusable `TwoPortBridgePair` now supports two simultaneously
integrated *passive corpus oscillators* with positive masses, stiffness,
viscous damping and two signed attachment locations. Stored energy now
includes strings, bridge feet, each body mass/spring and every
port-to-body coupling spring; dissipated work includes mode and coupling
dashpots. Two symmetric/antisymmetric mode simulations with and without
damping passed the JUCE-free energy ledger at 44.1/48 kHz, with
maximum numerical step residual about 8.8e-21 J.

`TwoStringFiddleBridge::BridgeImpedance` exposes the bridge impedance and
an opt-in pair of **synthetic** (not measured) symmetric and rocking corpus
modes. These are exploratory and do not yet implement calibrated violin
bridge admittance or audio radiation. Continuous-crossing tests compare
modal damping against the failed constant-dashpot approach without
changing existing VST3/Play Mode.

Scientific design check: empirical bowed-violin models treat the
bowed string, measured bridge driving-point admittance and radiation
transfer as three different physical subsystems. See Sterling & Bocko,
"Empirical Physical Modeling for Bowed String Instruments" (2010),
DOI:10.1109/ICASSP.2010.5495754, and Smith,
"Physical Audio Signal Processing", section 9.5.2.
