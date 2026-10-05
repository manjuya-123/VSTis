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
- a shared passive 20-mode bridge/body admittance with a damped high-order modal tail
- a two-coordinate bridge closure: common vertical translation plus weaker left/right rocking, with each string coupled by its physical bridge position
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

`FiddleModelPitchRegression` checks single-note pitch plus a four-string/three-contact
position matrix under the full bridge/body load. The calibrated matrix now requires
<= 2 cents maximum error and <= 0.8 cents mean absolute error. The bridge reflection
phase is derived from the translation + rocking modal admittance rather than an
empirical cents offset. `FiddleModelNoteStackSmoke` checks overlapping-note/legato
priority without any JUCE dependency.

## Next milestones

1. Add reference-listening comparisons against external fiddle recordings where licensing permits.
2. Expand MPE/per-note expression beyond the current pitch-bend path.
3. Continue refining performance gestures without exposing solver coefficients.

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
| Output Level | make the instrument sit at a practical DAW level | post-model gain only; default +18 dB |

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

The plugin's final **Output** control is a post-model listening level: the current default
is +18 dB and the range extends to +30 dB. It changes loudness only and does not feed back
into bow/string/body mechanics.

The body model now uses a denser, increasingly damped high-order modal tail above 3.5 kHz.
Stereo output is not chorus or doubled strings: both channels share the same string/body
mechanics. The bridge now has both vertical translation and a weaker rocking coordinate,
so outer and inner strings load the body differently before two nearby directional
radiation responses form the stereo output. Rocking radiation is wavelength-dependent:
low frequencies stay closer to the mono body core while upper body/air frequencies radiate
a little more directionally. Regression checks require the 2.5-8 kHz Side/Mid ratio to be
at least 1.4x the 100-800 Hz ratio while the broadband Side/Mid remains deliberately
subtle, so mono sum retains the instrument's center image.

The editor uses the four-string instrument view as its primary performance display.
A compact piano-style Play Key Map is also shown as a controller legend so Fiddle Play's
two keyboard regions are immediately visible. The instrument view shows:

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
- Hair+Rosin -> static grip, sliding friction, contact-state relaxation, and microscopic roughness injected at the bow/string contact. The roughness is now a deterministic spatial surface attached to the travelling bow hair: faster motion traverses the same micro-profile faster, and a bow reversal retraces that profile in the opposite direction. Its audible level still follows grip utilization, slip speed, normal force, contact temperature and Bow Contact position; it is not noise mixed into the output. Light under-gripped pressure therefore retains audible instability instead of simply becoming quieter.

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


## Stopped-string termination

Fingered notes now include additional energy loss at the stopped-string termination.
A very short extra loss is applied when the finger first lands, then relaxes to a smaller
steady fingertip loss. This improves separation in quick left-hand passages without
using an output amplitude envelope or resetting the string state.


## Fiddle Play Mode

Fiddle Play Mode separates the modeled player's two hands.

### Fingering Keys

MIDI G3 and above describe the left hand. They select stopped/open notes on the four
physical G/D/A/E strings but do not start the bow by themselves.

Up to four held fingering notes are voiced onto distinct physical strings. For example:

- E4 alone -> E4 stopped on the D string
- E4 + B4 -> stopped D+A double-stop fingering
- G3 + D4 + A4 + E5 -> all four open strings prepared

Fingering-key velocity is intentionally not used as bow loudness. It describes a left-hand
selection, not a right-hand gesture.

### Bow Action Keys

C2-B2 is the right-hand action octave:

- C2 — Down Bow
- C#2 — Nashville Shuffle
- D2 — Up Bow
- E2 — Short Stroke
- F2 — Tremolo
- G2 — Drone Bow / balanced adjacent-string bow
- A2 — Accent Stroke
- A#2 — Chop
- B2 — Release

C3-F#3 is intentionally unused in Fiddle Play, creating a visible gap before the
left-hand fingering region begins at G3. Bow Action velocity changes bow energy/pressure
around the panel setting.

Unlike sample-library keyswitches, these keys do not choose prerecorded articulations.
They change the state and motion of the physical bow/string model while the left-hand
fingering remains persistent.

The CI listening artifact `09_fiddle_play_demo.wav` demonstrates one held E4+B4
fingering across the main Fiddle Play gestures. `11_fiddle_gesture_showcase.wav` is the
focused comparison render: Short → Accent → Chop → Tremolo → Nashville Shuffle, with
silence between gestures for quick auditioning.


## Fiddle Play mode

Fiddle Play is a dedicated performance mode rather than a conventional keyswitch bank.

The keyboard is split by musical role:

- **G3 and above (MIDI 55+) = left hand.** Held notes are voiced onto real G/D/A/E
  strings. One note chooses a stopped string; two or more held notes form a physical
  double-stop/chord fingering where possible.
- **C2-B2 area = right hand.** These keys do not choose pitch. They perform bow actions:
  Down Bow, Up Bow, Short Stroke, Tremolo, Drone Bow, Accent, Chop, and Release.
- Releasing a bow-action key can stop a sustained bow gesture without erasing the held
  left-hand fingering.
- Changing the fingering while a bow gesture is held moves the stopped positions on the
  persistent strings rather than launching unrelated synth voices.

Current action map:

| MIDI note | Action |
| --- | --- |
| C2 / 36 | Down Bow |
| C#2 / 37 | Nashville Shuffle |
| D2 / 38 | Up Bow |
| E2 / 40 | Short Stroke |
| F2 / 41 | Tremolo |
| G2 / 43 | Drone Bow |
| A2 / 45 | Accent Stroke |
| A#2 / 46 | Chop / physical bow-string collision |
| B2 / 47 | Release |

This is intentionally closer to a playable instrument controller than to detailed DAW
automation. The action keys expand into physical bow direction, speed, force, duration,
and string-focus behaviour inside the model.


## Fiddle Play performance mode

Fiddle Play separates the two hands instead of treating every MIDI note as a finished
sampled articulation.

Left hand:
- MIDI G3 and above describes pitches to be stopped on the four physical strings.
- One to four held notes are voiced across G/D/A/E with continuity preference.
- With one held fingering note and the String Focus control left at its centred/default
  position, Fiddle Play automatically leans the physical bow toward the primary string
  (about +/-0.95 focus). Moving String Focus away from centre is an explicit manual
  override. Multi-note fingerings keep centre balance available.
- Sustain pedal (CC64) is Fingering Hold: the current left-hand shape stays on the
  strings after the fingering keys are released, so bow actions can be sequenced
  independently. Releasing the pedal removes only fingers whose keys are no longer held.

Right hand action row:
- C2 / 36 — Down Bow
- C#2 / 37 — Nashville Shuffle
- D2 / 38 — Up Bow
- E2 / 40 — Short Stroke
- F2 / 41 — Tremolo
- G2 / 43 — Drone Bow / explicitly balanced adjacent pair (overrides single-note auto-focus)
- A2 / 45 — Accent Stroke
- A#2 / 46 — Chop
- B2 / 47 — Release

The action row drives bow physics; it does not switch to prerecorded or alternate
synthesis voices. Slurs are made by keeping a bow action active while changing the
left-hand fingering.

The regression artifacts include `16_single_focus_vs_drone.wav`, ordered
**automatic single-string focus -> Drone Bow**, so the difference can be auditioned
without changing the left-hand E4-on-D fingering. `15_string_identity_pair_side.wav`
also compares stopped D-string A4 approached from the D/A side, the same stopped note
approached from the G/D side, and open A4; this isolates how adjacent-pair choice and
the open-A unison contribute to colour.


### Direct performance UI

Fiddle Play can now be driven without a second MIDI controller:

- **Fingering Hold** is available as an automatable GUI toggle as well as CC64.
- The bow-action octave in the piano-style Play Key Map is clickable.
- Clicking a sustained gesture (Down / Up / Tremolo / Shuffle / Drone) holds it until mouse-up.
- The same map shows the unused C3-F#3 gap and the G3-C8 fingering region.
- Double stops and chords light all effective fingering keys at once; the current note
  is brighter, and Fingering Hold keeps the latched left-hand shape visible.
- Short / Accent / Chop use physical one-shot timing in the engine. Releasing their
  action key does not shorten the gesture or remove its pressure/speed/response settings
  halfway through. Short leaves the string with a rounded bow lift; Accent combines a
  stronger first bite with a faster/braked release. Chop adds a short transverse impact
  at the bowing point before the strongly damped lift, so its percussive transient enters
  through the string waveguides rather than through an output-layer click.

GUI commands are queued to the audio thread through atomics; the editor never calls the
physical DSP directly from the message thread.


### Shuffle Bow

C#2 / MIDI 37 triggers a reduced-order fiddle shuffle gesture. The engine repeats
mirrored long-short-short bow cells by changing the physical bow direction and the
stroke energy. MIDI velocity increases the subdivision rate and gesture energy.

Shuffle is intentionally implemented as bow motion acting on the currently held
physical fingering; it is not an articulation sample or alternate synth voice.


### Rosin contact visualization

The dedicated string view also visualizes the reduced rosin contact state. The normal
performance UI does not expose contact temperature in degrees; instead the bow colour
and status text move through **Rosin Cool / Working / Hot** according to the hottest
contact in the active string pair.

The regression artifact also includes `12_rosin_texture_showcase.wav`, ordered
**Medium Rosin -> Dry Light Grip -> High Grip Rosin**, so the contact-material texture
can be auditioned without searching through the full comparison render. A second file,
`13_rosin_performance_showcase.wav`, is ordered **Slow Bow -> Fast Bow -> Light Pressure
-> Firm Pressure** and isolates how player gestures change the same microscopic contact
texture.

This is a visualization of the same state used by the bow/string friction model, not a
separate cosmetic meter.
