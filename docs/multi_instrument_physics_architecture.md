# VSTis: multi-instrument physical modelling architecture

This repository is **not fiddle-only**. FiddleModel is one instrument among
future physically modelled instruments. The old fiddle engine remains an
isolated A/B reference and is not the foundation of the new sound core.

## Layer boundaries

- `shared/physical/`: JUCE-free, instrument-neutral mechanical integrators,
  coupled states, diagnostics and verified work/energy accounting. No bow,
  fingering MIDI, note names, presets, acoustic radiation or instrument UI.
- `instruments/<name>/Source/`: instrument-specific geometry, exciters,
  contact physics, bridge, body, radiation, and performance interpretation.
  Other instruments may use entirely different solvers.
- Plugin/application layers: JUCE VST3 and Standalone integration are
  independent of physical system implementation.

## Rebuild milestone 1 (numerical foundation, NOT accepted tone)

`shared/physical/ImplicitString1D.h` is a new fixed-end one-polarisation
linear-string solver. It solves a tridiagonal implicit-midpoint update and
preserves the same nodal displacement and velocity across force and contact
changes. Bow, finger or other instrument forces act on that state rather than
triggering an auxiliary oscillator or retuning a delay line.

The continuous starting point is:

    mu*q_tt = T*q_xx - d*q_t + Fb*delta(x-xb) + Ff*delta(x-xf)

Discrete energy/work identity:

    E(next)-E(now) = dt*(Fb*vb_mid + Ff*vf_mid - d*SUM(vnode_mid^2))

`instruments/FiddleModel/Source/Rebuild/BowedStringPilot.h` is a fiddle-only,
one-string exploratory adapter. Both prescribed bow traction and compliant
finger force are solved together on the *same* mechanical field. A stopped
note maps to a finger coordinate, not a DSP state reset.

`FiddleRebuildNumerics` tests free-field energy drift, forced work,
finite energy under changing finger pressure, and presence of both stick and
sliding phases. The WAV is **raw rigid-bridge reaction force**, not calibrated
radiated violin audio. Passing these tests establishes only limited
mathematical correctness, not realistic timbre, stable Helmholtz motion or
natural crossing.

## Missing physical elements

The pilot has only one transverse polarisation, point bow, simplified
static/regularised sliding friction, bilateral (not unilateral) finger
spring, ideal linear string, rigid bridge, and no body radiation. It does
not implement independent bow-hair mechanics, rosin history, torsion,
dispersion, violin corpus or multiple strings. The new instrument is **not
yet implemented in the VST3**.

## Next acceptance gates

1. Implement two transverse polarisations, stiffness, frequency-dependent
   losses and nonpenetrating fingerboard contact with work ledger.
2. Introduce physically validated bow-hair/rosin dynamics and compare
   single-string Helmholtz behaviour with recorded performances.
3. Add a genuinely common passive bridge, two-string crossing A-E-A,
   sustained legato, and energy/work measurements at all contacts.
4. Extend to four strings, a calibrated body and directional radiation.
5. Wire in existing fiddle Play Mode as gestures without note-triggered
   resets, then audition the exact 32-bar Hillwind Reel MIDI.
6. Require **human acceptance of natural sound and no recurring 'pon'** in
   addition to successful builds and regressions.

Technical reading: Desvages & Bilbao (2016),
Two-Polarisation Physical Model of Bowed Strings with Nonlinear Contact
and Friction Forces; https://doi.org/10.3390/app6050135 .
The article motivates an energy-balanced approach; its complete
two-polarisation model has NOT been implemented or claimed here.
