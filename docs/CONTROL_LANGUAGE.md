# FiddleModel control-language contract

The plug-in UI is written for a player, not for the DSP implementation.

## Rules

1. A normal performance control must describe an action a player can imagine doing.
2. A label should predict an audible consequence without requiring knowledge of the solver.
3. Raw coefficients, filter poles, impedances, friction-state variables, bridge-mode gains,
   contact stiffness, and numerical integration parameters stay internal.
4. Use numbers only when the unit already has musical meaning: semitones, cents, Hz,
   milliseconds, or a clearly defined musical ratio.
5. Prefer directional/spatial wording for spatial actions:
   Fingerboard ↔ Bridge, G ↔ D, D ↔ A, A ↔ E.
6. Advanced diagnostics may expose physical values later, but they belong in a separate
   diagnostic view and must not be presented as the normal instrument controls.

## Current player controls

- Bow Pressure — how firmly the bow is pressed into the strings.
- Bow Speed — how quickly the bow moves.
- Bow Response — soft/gradual versus quick/crisp onset.
- Bow Contact — where the bow touches between fingerboard side and bridge side.
- String Focus — which member of the currently active adjacent-string pair receives more bow.
- Pitch Bend Range — musically meaningful wheel travel in semitones.

The active String Focus pair is shown by string names (for example D ↔ A), not merely
"lower" and "upper".


Additional player controls:
- Vibrato Width — narrow/natural/wide left-hand pitch movement.
- Vibrato Pace — slow/natural/quick left-hand rocking speed.

These are player descriptions; the internal cents and modulation frequency remain
implementation details rather than normal performance parameters.


## MIDI gesture language

- Mod Wheel -> more Vibrato
- Aftertouch -> more Bow Pressure
- Pitch Wheel -> fingered pitch movement

Normal UI copy should describe the musical gesture first. Controller numbers and DSP
scaling are implementation details unless the user explicitly opens a diagnostics or
MIDI-mapping view.


## Dedicated instrument view

The primary visual metaphor remains the bowed instrument itself. A compact piano-style
**Play Key Map** is allowed as a controller legend because Fiddle Play divides the MIDI
keyboard by hand role. It must remain secondary to the four-string instrument view and
must clearly show C2-B2 as bow actions, C3-F#3 as unused, and G3-C8 as fingering.
When a double stop or chord is held, all effective fingering keys light together; the
current fingering is emphasized. Fingering Hold keeps the latched shape lit after the
physical keys are released.
Show the player which string is active, where it is stopped, where the bow contacts
the string pair, and the bow direction.

## Material controls

Material selectors are allowed in the normal UI because they describe a physical object
the player can understand. They must alter the physical model, not merely call an EQ
preset. If a profile is exploratory rather than measured from a real material, label it
Experimental.


## Bow Strokes

Normal UI exposes bowing intent, not a direction-state machine:

- Fiddle Auto — separate notes alternate, overlapping legato stays connected.
- Connected — keep the same bow direction.
- Alternate — reverse on every new Note On.

Fiddle Auto is the normal default for keyboard performance.


## Materials vs performance controls

Materials describe what the instrument/bow is made from; performance controls describe
what the player is doing.

Current material-facing controls:
- Body
- Bow Stick
- Hair / Rosin
- String Core

String Core offers Synthetic core, Steel core, and Gut-like profiles. The UI should
describe their playing consequence rather than expose loop-loss or allpass coefficients.


## Fiddle Play Mode language

Use the term **Fiddle Play Mode**, not "keyswitch mode", in the normal UI.

The action keys are closer to right-hand gesture commands than to sample keyswitches:
they operate the physical bow state while Fingering Keys maintain left-hand string stops.

Current action row:
C2 Down Bow / C#2 Nashville Shuffle / D2 Up Bow / E2 Short Stroke /
F2 Tremolo / G2 Drone Bow / A2 Accent Stroke / A#2 Chop / B2 Release.

G3 through C8 are Fingering Keys. C3 through F#3 are intentionally unused in
Fiddle Play so the left- and right-hand regions stay visually separated.

The Play Key Map may expose these assignments directly because MIDI note names are
musically meaningful controller coordinates, not solver parameters.


## Play modes

- **Fiddle Play (performance)** is the default. MIDI notes above G3 describe left-hand
  fingering/chord shape. Low action keys describe right-hand bow gestures.
- **Chromatic (keyboard)** is a compatibility mode where ordinary MIDI Note On directly
  requests a pitch.

Fiddle Play should always be described as an instrument-performance mode, not as a
"keyswitch bank". The action keys do not swap samples; they expand into physical changes
to bow direction, speed, pressure, duration, string focus, and persistent string state.


## Fiddle Play mode

Fiddle Play is a two-hand performance workflow:

- Fingering region (G3+) describes where fingers sit on the four strings.
- Action region (C2-B2) describes what the bow hand does.
- CC64 is labelled Fingering Hold, not Sustain, because it latches the left-hand shape.
- Slur is not a hidden articulation switch: keep the bow moving and change the fingers.
- Drone Bow means physically bow the selected adjacent pair with balanced focus.
- Short / Accent / Chop are gestures built from bow force, speed and contact duration,
  not alternate rendered samples.


The dedicated Fiddle Play Key Map is interactive on the bow-action octave. Mouse
gestures use the same Down / Shuffle / Up / Short / Tremolo / Drone / Accent / Chop /
Release paths as the MIDI action row, so the GUI is not a separate audition-only
sound path.


- Shuffle — a repeating long-short-short physical bow gesture on the current fingering.
  Velocity changes the subdivision rate/energy. Do not describe it as a sampled
  articulation or expose its internal scheduler counters in the normal UI.


## Physical-state visualization

Internal physical state may be shown when it helps playing, but should be translated
into musician-readable language. Rosin contact temperature is therefore shown as
Cool / Working / Hot and by bow colour, not as a thermal solver coefficient or a
degrees-Celsius performance control.

The long-short-short action on C#2 is labelled Nashville Shuffle. It remains a bow
gesture acting on the current fingering, not a switched articulation sample.
