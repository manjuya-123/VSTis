juce_add_plugin(FiddleModel
    COMPANY_NAME "VSTis"
    BUNDLE_ID "com.vstis.fiddlemodel"
    PLUGIN_MANUFACTURER_CODE VSTI
    PLUGIN_CODE FDL1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Fiddle Model"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
)

target_sources(FiddleModel PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/Dsp/FiddleEngine.cpp
    Source/Dsp/FiddleEngine.h
)

target_include_directories(FiddleModel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(FiddleModel PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(FiddleModel
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
