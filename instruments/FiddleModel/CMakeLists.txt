if (NOT VSTIS_CORE_ONLY)
    set(FIDDLE_BUILD_ID "local")
    find_package(Git QUIET)
    if (GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short=10 HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE FIDDLE_BUILD_ID
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

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
        Source/Dsp/FiddlePlayLayout.h
        Source/Dsp/FiddleFingeringVoicer.h
        Source/Dsp/FiddleGestureProfile.h
        Source/Dsp/detail/BowGeometryMapper.h
    )
    
    target_include_directories(FiddleModel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    
    target_compile_definitions(FiddleModel PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        FIDDLE_BUILD_ID="${FIDDLE_BUILD_ID}"
    )
    
    target_link_libraries(FiddleModel
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    if (BUILD_TESTING)
        juce_add_console_app(FiddleModelProcessorMidiRegression
            PRODUCT_NAME "Fiddle Processor MIDI Regression"
        )

        target_sources(FiddleModelProcessorMidiRegression PRIVATE
            Tests/FiddleProcessorMidiRegression.cpp
            Source/PluginProcessor.cpp
            Source/PluginProcessor.h
            Source/PluginEditor.cpp
            Source/PluginEditor.h
            Source/ParameterPresentation.h
            Source/Dsp/FiddleEngine.cpp
            Source/Dsp/FiddleEngine.h
            Source/Dsp/MidiNoteStack.h
            Source/Dsp/BowStrokePolicy.h
            Source/Dsp/FiddlePlayLayout.h
            Source/Dsp/FiddleFingeringVoicer.h
            Source/Dsp/FiddleGestureProfile.h
            Source/Dsp/detail/BowGeometryMapper.h
        )

        target_include_directories(FiddleModelProcessorMidiRegression PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Source
        )

        target_compile_definitions(FiddleModelProcessorMidiRegression PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JucePlugin_Name="Fiddle Model"
            FIDDLE_BUILD_ID="${FIDDLE_BUILD_ID}"
        )

        target_link_libraries(FiddleModelProcessorMidiRegression
            PRIVATE juce::juce_audio_utils juce::juce_dsp
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags
        )

        add_test(
            NAME FiddleModelProcessorMidiRegression
            COMMAND FiddleModelProcessorMidiRegression
                ${CMAKE_CURRENT_BINARY_DIR}/processor-midi-regression
        )
    endif()
endif()

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
    add_test(
        NAME FiddleModelPitchRegression
        COMMAND FiddleModelPitchRegression
                ${CMAKE_CURRENT_BINARY_DIR}/regression-audio
    )

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

    add_executable(FiddleModelBowContactStateSmoke
        Tests/BowContactStateSmoke.cpp
        Source/Dsp/detail/BowContact.h
    )
    target_include_directories(FiddleModelBowContactStateSmoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelBowContactStateSmoke PRIVATE cxx_std_17)
    add_test(NAME FiddleModelBowContactStateSmoke COMMAND FiddleModelBowContactStateSmoke)

    add_executable(FiddleModelPlayModeSmoke
        Tests/FiddlePlayModeSmoke.cpp
        Source/Dsp/FiddlePlayLayout.h
        Source/Dsp/FiddleFingeringVoicer.h
    )
    target_include_directories(FiddleModelPlayModeSmoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelPlayModeSmoke PRIVATE cxx_std_17)
    add_test(NAME FiddleModelPlayModeSmoke COMMAND FiddleModelPlayModeSmoke)

    add_executable(FiddleModelGestureProfileSmoke
        Tests/FiddleGestureProfileSmoke.cpp
        Source/Dsp/FiddleGestureProfile.h
        Source/Dsp/FiddlePlayLayout.h
    )
    target_include_directories(FiddleModelGestureProfileSmoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelGestureProfileSmoke PRIVATE cxx_std_17)
    add_test(NAME FiddleModelGestureProfileSmoke COMMAND FiddleModelGestureProfileSmoke)

    add_executable(FiddleModelPlayPerformanceRegression
        Tests/FiddlePlayPerformanceRegression.cpp
        Source/Dsp/FiddleEngine.cpp
        Source/Dsp/FiddleEngine.h
    )
    target_include_directories(FiddleModelPlayPerformanceRegression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelPlayPerformanceRegression PRIVATE cxx_std_17)
    add_test(
        NAME FiddleModelPlayPerformanceRegression
        COMMAND FiddleModelPlayPerformanceRegression
                ${CMAKE_CURRENT_BINARY_DIR}/regression-audio/09_fiddle_play_demo.wav
    )

    add_executable(FiddleModelGestureOnsetRegression
        Tests/FiddleGestureOnsetRegression.cpp
        Source/Dsp/FiddleEngine.cpp
        Source/Dsp/FiddleEngine.h
        Source/Dsp/FiddleGestureProfile.h
        Source/Dsp/FiddlePlayLayout.h
    )
    target_include_directories(FiddleModelGestureOnsetRegression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelGestureOnsetRegression PRIVATE cxx_std_17)
    add_test(
        NAME FiddleModelGestureOnsetRegression
        COMMAND FiddleModelGestureOnsetRegression
                ${CMAKE_CURRENT_BINARY_DIR}/regression-audio
    )

    add_executable(FiddleModelControlSurfaceRegression
        Tests/FiddleControlSurfaceRegression.cpp
        Source/Dsp/FiddleEngine.cpp
        Source/Dsp/FiddleEngine.h
    )
    target_include_directories(FiddleModelControlSurfaceRegression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelControlSurfaceRegression PRIVATE cxx_std_17)
    add_test(
        NAME FiddleModelControlSurfaceRegression
        COMMAND FiddleModelControlSurfaceRegression
                ${CMAKE_CURRENT_BINARY_DIR}/regression-audio
    )

    add_executable(FiddleModelMaterialRegression
        Tests/FiddleMaterialRegression.cpp
        Source/Dsp/FiddleEngine.cpp
        Source/Dsp/FiddleEngine.h
    )
    target_include_directories(FiddleModelMaterialRegression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(FiddleModelMaterialRegression PRIVATE cxx_std_17)
    add_test(NAME FiddleModelMaterialRegression COMMAND FiddleModelMaterialRegression)
endif()
