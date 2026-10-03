# FiddleModel

Physical-modeling fiddle / violin-family VST instrument.

## Current implementation milestone

\`Source/Dsp/FiddleEngine\` is now a native C++ physical-model core rather than the
initial oscillator smoke test. It is deliberately independent of JUCE and currently
contains:

- four persistent bidirectional string waveguides
- linear fractional-delay pitch control
- per-string passive loss filters and mild allpass dispersion
- nonlinear stateful bow/string friction contacts
- up to two simultaneously bowed adjacent strings
- Pressure-aware Performance Balance calibrated to physical bow angle
- bridge curvature + compliant bow-hair contact geometry
- total bow-force conservation through geometric contact solving
- a shared passive 12-mode bridge/body admittance
- sympathetic excitation through the shared bridge
- continuous speaking-length changes when MIDI pitch changes strings/fingering
- internal smoothing for Pressure, Speed, Attack, Position and Balance
- bow release with physical-state decay rather than voice destruction

The JUCE processor renders around MIDI event sample offsets so note transitions begin
at the correct position inside each host audio block. A fixed-size, allocation-free MIDI
note stack implements last-note-priority legato: releasing an older/background note does
not stop the active note, and releasing the active note returns to the newest held note.

## Current note/string mapping

The engine selects the highest open string that can play the incoming MIDI frequency:

- below D4: G string, G+D bow pair
- D4 .. below A4: D string, D+A bow pair
- A4 .. below E5: A string, A+E bow pair
- E5 and above: E string, A+E bow pair

The selected string is fingered continuously to the note frequency. Its adjacent string
remains physically present and can act as a drone/sympathetic string according to Balance.
This mapping is an intermediate performance model and will become configurable later.

## Performance controls

- **Pressure**: maps to total bow normal force.
- **Speed**: maps to bow velocity.
- **Attack**: maps to bow acceleration.
- **Position**: fingerboard-side to bridge-side bow position.
- **Balance**: maps through a precomputed Pressure x Balance calibration LUT to a physical bow angle. Bridge curvature and bow-hair compliance then determine contact forces. The center therefore stays close to equal normalized pressure across the active pair as Pressure changes.

## Automated core test

\`FiddleModelCoreSmoke\` is a JUCE-independent CTest target. It verifies:

- finite/non-silent output
- D+A pair selection for an E4-on-D + open-A drone case
- conserved physical normal-force range
- Balance direction
- release-tail decay

The same test has also been compiled with AddressSanitizer/UBSan in the development
environment.

`FiddleModelPitchRegression` checks E4/F#4/G4 on the D string with a period-correlation
pitch estimator and currently requires <= 7 cents error. `FiddleModelNoteStackSmoke`
checks overlapping-note/legato priority without any JUCE dependency.

## Next milestones

1. Add regression rendering against reference WAV metrics.
2. Improve phase/pitch compensation at the bridge load where needed.
3. Add note-stack/legato policy and MPE mappings.
4. Replace the generic JUCE editor with the performance UI.

The Bow Angle calibration LUT is generated in `prepare()` rather than on the audio thread. The audio path only bilinearly interpolates the LUT and performs the final contact-depth solve.
