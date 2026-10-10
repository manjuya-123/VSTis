# JUCE- and instrument-independent mechanical solvers.
add_library(vstis_physical_core INTERFACE)
target_include_directories(vstis_physical_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(vstis_physical_core INTERFACE cxx_std_17)
