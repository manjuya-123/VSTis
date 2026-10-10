# JUCE- and instrument-independent mechanical solvers.
add_library(vstis_physical_core INTERFACE)
target_include_directories(vstis_physical_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(vstis_physical_core INTERFACE cxx_std_17)

if (BUILD_TESTING)
    add_executable(VSTisSharedBridgeNumerics Tests/SharedBridgeNumerics.cpp)
    target_link_libraries(VSTisSharedBridgeNumerics PRIVATE vstis_physical_core)
    add_test(NAME VSTisSharedBridgeNumerics COMMAND VSTisSharedBridgeNumerics)
endif()
