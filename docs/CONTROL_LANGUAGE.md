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
