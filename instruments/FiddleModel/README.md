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
- sample-accurate MIDI Pitch Bend retuning without resetting waveguide state
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

Pitch Bend range is exposed as a 1-24 semitone parameter (default ±2). Pitch-wheel events retune the current speaking length without reselecting the physical string, preserving the ongoing bow/string state.


## UI philosophy

The plug-in deliberately does not expose solver coefficients as performance controls.
The first dedicated editor presents actions and audible consequences in player language:

| UI control | What the player means | Internal effect |
| --- | --- | --- |
| Bow Pressure | press the bow lighter/harder | total normal force, then geometry/contact solve |
| Bow Speed | move the bow slower/faster | physical bow velocity |
| Bow Response | make the onset soft/crisp | bow acceleration |
| Bow Contact | move toward fingerboard/bridge | physical bow position on the speaking length |
| String Focus | lean toward one string of the pair | pressure-aware physical bow-angle calibration |
| Pitch Bend Range | choose usable wheel travel | speaking-length retune range |

The UI hides normalized 0..1 values and displays player words such as Light, Natural,
Firm, Near bridge, and Upper-biased. Numerical values remain only where the unit itself
is meaningful to a musician, for example ±2 semitones.


Pitch-wheel state no longer participates in physical-string selection at Note On.
The MIDI note first selects the string/fingering, then Pitch Bend retunes that same
speaking length. This prevents a downward bend from accidentally moving an A-string
note onto the D string.


Left-hand vibrato is modeled as continuous speaking-length motion on the fingered
primary string. The normal GUI exposes Vibrato Width and Vibrato Pace rather than
LFO depth/rate. Open strings intentionally ignore this finger vibrato.


## MIDI performance gestures

- Mod Wheel increases Vibrato Width from the current panel setting.
- Channel Aftertouch adds Bow Pressure, up to 30% of the full control range.
- Pitch Bend changes the speaking length of the already selected physical string.

These mappings are intentionally described as player gestures rather than DSP modulation
sources.


## Dedicated fiddle view

The editor now visualises the instrument as four strings rather than as a piano keyboard.
It shows:

- G / D / A / E strings
- the currently selected physical string
- approximate stopped-string finger position
- the active adjacent bow pair
- bow contact position between fingerboard side and bridge side
- current up-bow / down-bow direction

The default Bow Strokes mode is **Alternate**. Each new MIDI Note On reverses bow
direction without resetting string/body state, which is intended for fast fiddle
detaché/shuffle-style passages. **Connected** keeps the current bow direction for
slurred/legato playing.

## Materials

The normal editor now has Body / Bow Stick / Hair+Rosin material selectors.
They are connected to physical model quantities rather than post-EQ:

- Body -> modal frequency, damping and admittance
- Bow Stick -> effective acceleration/reversal response
- Hair+Rosin -> static grip, sliding friction and contact-state relaxation

Experimental profiles are labelled as such; they are not presented as calibrated
measurements of real commercial materials.


## Fiddle Auto bowing

The default Bow Strokes mode is now **Fiddle Auto**.

- Separate/non-overlapping notes alternate bow direction automatically.
- Overlapping legato notes keep the current bow direction.
- Connected forces same-direction phrasing.
- Alternate reverses on every Note On.

This is intended to make ordinary MIDI-keyboard performance closer to practical fiddle
bowing without requiring the player to automate bow direction manually.


### String Core material

The Materials section also includes String Core:

- Synthetic core — balanced baseline profile.
- Steel core — lower distributed loss and a quicker, more persistent response.
- Gut-like — higher distributed loss and a softer/slower response.

These profiles alter the physical string loop, including its loss and phase behaviour.
They are not post-EQ presets and are intentionally labelled as broad material profiles,
not calibrated copies of commercial string brands.
