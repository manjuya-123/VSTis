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
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ParameterPresentation.h
    Source/Dsp/FiddleEngine.cpp
    Source/Dsp/FiddleEngine.h
    Source/Dsp/MidiNoteStack.h
    Source/Dsp/BowStrokePolicy.h
    Source/Dsp/detail/BowGeometryMapper.h
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

if (BUILD_TESTING)
    add_executable(FiddleModelCoreSmoke
        Tests/FiddleEngineSmoke.cpp
        Source/Dsp/FiddleEngine.cpp
        Source/Dsp/FiddleEngine.h
    )
    target_include_directories(FiddleModelCoreSmoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelCoreSmoke PRIVATE cxx_std_17)
    add_test(NAME FiddleModelCoreSmoke COMMAND FiddleModelCoreSmoke)

    add_executable(FiddleModelPitchRegression
        Tests/FiddleEnginePitchRegression.cpp
        Source/Dsp/FiddleEngine.cpp
        Source/Dsp/FiddleEngine.h
    )
    target_include_directories(FiddleModelPitchRegression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelPitchRegression PRIVATE cxx_std_17)
    add_test(NAME FiddleModelPitchRegression COMMAND FiddleModelPitchRegression)

    add_executable(FiddleModelNoteStackSmoke
        Tests/NoteStackSmoke.cpp
        Source/Dsp/MidiNoteStack.h
    )
    target_include_directories(FiddleModelNoteStackSmoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelNoteStackSmoke PRIVATE cxx_std_17)
    add_test(NAME FiddleModelNoteStackSmoke COMMAND FiddleModelNoteStackSmoke)

    add_executable(FiddleModelAudioRegression
        Tests/FiddleAudioRegression.cpp
        Source/Dsp/FiddleEngine.cpp
        Source/Dsp/FiddleEngine.h
    )
    target_include_directories(FiddleModelAudioRegression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelAudioRegression PRIVATE cxx_std_17)
    add_test(
        NAME FiddleModelAudioRegression
        COMMAND FiddleModelAudioRegression ${CMAKE_CURRENT_BINARY_DIR}/regression-audio
    )

    add_executable(FiddleModelFastPassageRegression
        Tests/FiddleFastPassageRegression.cpp
        Source/Dsp/FiddleEngine.cpp
        Source/Dsp/FiddleEngine.h
    )
    target_include_directories(FiddleModelFastPassageRegression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelFastPassageRegression PRIVATE cxx_std_17)
    add_test(
        NAME FiddleModelFastPassageRegression
        COMMAND FiddleModelFastPassageRegression
                ${CMAKE_CURRENT_BINARY_DIR}/regression-audio/08_fast_fiddle_passage.wav
    )

    add_executable(FiddleModelBowStrokePolicySmoke
        Tests/BowStrokePolicySmoke.cpp
        Source/Dsp/BowStrokePolicy.h
    )
    target_include_directories(FiddleModelBowStrokePolicySmoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelBowStrokePolicySmoke PRIVATE cxx_std_17)
    add_test(NAME FiddleModelBowStrokePolicySmoke COMMAND FiddleModelBowStrokePolicySmoke)

    add_executable(FiddleModelMaterialRegression
        Tests/FiddleMaterialRegression.cpp
        Source/Dsp/FiddleEngine.cpp
        Source/Dsp/FiddleEngine.h
    )
    target_include_directories(FiddleModelMaterialRegression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelMaterialRegression PRIVATE cxx_std_17)
    add_test(NAME FiddleModelMaterialRegression COMMAND FiddleModelMaterialRegression)
endif()
