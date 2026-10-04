# VSTis

Visual Studio 2022 / CMake / JUCE based VST3 instrument monorepo.

## Layout

    VSTis/
    ├─ CMakeLists.txt
    ├─ CMakePresets.json
    ├─ cmake/
    │  └─ JUCE.cmake
    ├─ instruments/
    │  └─ FiddleModel/
    ├─ shared/
    ├─ tools/
    └─ docs/

- `instruments/`: one directory per VST instrument.
- `shared/`: reusable DSP and utility code.
- `cmake/`: common build configuration.
- `tools/`: developer scripts.
- `docs/`: project-wide engineering notes.

## Development environment

- Visual Studio 2022
- MSVC v143
- C++17
- CMake 3.22+
- JUCE 9.0.3
- Windows x64
- VST3 + Standalone

The plugin runtime is native C++; .NET is not required. Existing .NET 7 tooling can remain unchanged.

## Local verification

On the Windows/Visual Studio 2022 development machine, run:

    powershell -ExecutionPolicy Bypass -File tools/test_core.ps1

This configures the JUCE-independent physical core, builds and runs the regression suite,
prints the audio/gesture metrics, and leaves the listening WAVs under
`build/vs2022-core/instruments/FiddleModel/regression-audio`.

## Instruments

### FiddleModel

Physical-modeling fiddle/violin-family instrument under active development.
