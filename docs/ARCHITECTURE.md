# VSTis architecture

VSTis is a monorepo for multiple VST instruments.

Instrument DSP should remain as independent of JUCE as practical.
JUCE owns host-facing concerns: VST3/Standalone wrappers, MIDI, parameters/state, GUI, and device/host integration.

## Adding another instrument

Create a sibling directory under `instruments/` with its own CMake target.
Move code into `shared/` only after a second instrument has a real need for the same abstraction.
