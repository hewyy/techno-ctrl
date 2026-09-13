cmake_minimum_required(VERSION 3.22)

project(LivePatternSequencerMVP VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(LPS_FETCH_JUCE "Download JUCE automatically during configuration" ON)

if (LPS_FETCH_JUCE)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
else()
    find_package(JUCE CONFIG REQUIRED)
endif()

add_library(sequencer_core STATIC
    src/core/IPlayer.h
    src/core/IPlayerEditorModels.h
    src/core/IOutputRenderer.h
    src/core/NormalizedValue.cpp
    src/core/NormalizedValue.h
    src/core/PatternLibrary.cpp
    src/core/PatternLibrary.h
    src/core/PatternPlayer.cpp
    src/core/PatternPlayer.h
    src/core/RuntimeGraph.cpp
    src/core/RuntimeGraph.h
    src/core/SequencerTypes.h
    src/core/ModulationLibrary.cpp
    src/core/ModulationLibrary.h
    src/core/ModulationPlayer.cpp
    src/core/ModulationPlayer.h
    src/core/Voice.cpp
    src/core/Voice.h
)

target_include_directories(sequencer_core PUBLIC src)
target_compile_features(sequencer_core PUBLIC cxx_std_17)

add_executable(pattern_player_tests
    tests/PatternPlayerTests.cpp
)

add_executable(pattern_library_tests
    tests/PatternLibraryTests.cpp
)

add_executable(modulation_library_tests
    tests/ModulationLibraryTests.cpp
)

add_executable(modulation_player_tests
    tests/ModulationPlayerTests.cpp
)

add_executable(voice_tests
    tests/VoiceTests.cpp
)

add_executable(runtime_graph_tests
    tests/RuntimeGraphTests.cpp
)

add_executable(midi_buffer_transport_tests
    tests/MidiBufferTransportTests.cpp
    src/plugin/MidiBufferRenderer.cpp
    src/plugin/MidiBufferRenderer.h
)

add_executable(cv_buffer_renderer_tests
    tests/CvBufferRendererTests.cpp
    src/plugin/CvBufferRenderer.cpp
    src/plugin/CvBufferRenderer.h
)

target_link_libraries(pattern_library_tests PRIVATE sequencer_core)
target_link_libraries(pattern_player_tests PRIVATE sequencer_core)
target_link_libraries(modulation_library_tests PRIVATE sequencer_core)
target_link_libraries(modulation_player_tests PRIVATE sequencer_core)
target_link_libraries(voice_tests PRIVATE sequencer_core)
target_link_libraries(runtime_graph_tests PRIVATE sequencer_core)
target_include_directories(midi_buffer_transport_tests PRIVATE src)
target_link_libraries(midi_buffer_transport_tests
    PRIVATE
        sequencer_core
        juce::juce_audio_basics
)
target_include_directories(cv_buffer_renderer_tests PRIVATE src)
target_link_libraries(cv_buffer_renderer_tests
    PRIVATE
        sequencer_core
        juce::juce_audio_basics
)

enable_testing()
add_test(NAME pattern_player_tests COMMAND pattern_player_tests)
add_test(NAME pattern_library_tests COMMAND pattern_library_tests)
add_test(NAME modulation_library_tests COMMAND modulation_library_tests)
add_test(NAME modulation_player_tests COMMAND modulation_player_tests)
add_test(NAME voice_tests COMMAND voice_tests)
add_test(NAME runtime_graph_tests COMMAND runtime_graph_tests)
add_test(NAME midi_buffer_transport_tests COMMAND midi_buffer_transport_tests)
add_test(NAME cv_buffer_renderer_tests COMMAND cv_buffer_renderer_tests)

juce_add_plugin(LivePatternSequencer
    COMPANY_NAME "Hew"
    BUNDLE_ID "com.hew.livepatternsequencer"
    PLUGIN_MANUFACTURER_CODE HewB
    PLUGIN_CODE Lpsq
    FORMATS VST3
    PRODUCT_NAME "Live Pattern Sequencer MVP"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(LivePatternSequencer PRIVATE
    src/plugin/CvBufferRenderer.cpp
    src/plugin/CvBufferRenderer.h
    src/plugin/MidiBufferRenderer.cpp
    src/plugin/MidiBufferRenderer.h
    src/plugin/PatternLibraryFileStore.cpp
    src/plugin/PatternLibraryFileStore.h
    src/plugin/ModulationLibraryFileStore.cpp
    src/plugin/ModulationLibraryFileStore.h
    src/plugin/PluginEditor.cpp
    src/plugin/PluginEditor.h
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginProcessor.h
)

target_include_directories(LivePatternSequencer PRIVATE src)

target_link_libraries(LivePatternSequencer
    PRIVATE
        sequencer_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(LivePatternSequencer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
