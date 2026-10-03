# FiddleModel

Physical-modeling fiddle / violin-family VST instrument.

## Repository milestone

The checked-in engine is intentionally a small MIDI/audio smoke test so the VS2022 + JUCE + VST3 toolchain can be verified first.

The next milestone replaces `Source/Dsp/FiddleEngine` with the validated research DSP:

- four persistent string waveguides
- nonlinear stateful bow/string contacts
- frequency-dependent string loss and mild dispersion
- shared passive bridge / modal body admittance
- sympathetic resonance
- double stop with total bow-force conservation
- Performance Balance -> physical Bow Angle calibration
- continuous left-hand speaking-length changes

## Performance controls

Pressure / Speed / Attack / Position / Balance / Pitch / Finger Transition / Vibrato
