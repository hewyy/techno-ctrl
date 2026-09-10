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
    src/core/ITransport.h
    src/core/PatternLibrary.cpp
    src/core/PatternLibrary.h
    src/core/PatternPlayer.cpp
    src/core/PatternPlayer.h
    src/core/SequencerEngine.cpp
    src/core/SequencerEngine.h
    src/core/SequencerTypes.h
    src/core/VelocityModulationLibrary.cpp
    src/core/VelocityModulationLibrary.h
)

target_include_directories(sequencer_core PUBLIC src)
target_compile_features(sequencer_core PUBLIC cxx_std_17)

add_executable(sequencer_core_tests
    tests/SequencerEngineTests.cpp
)

add_executable(pattern_player_tests
    tests/PatternPlayerTests.cpp
)

add_executable(pattern_library_tests
    tests/PatternLibraryTests.cpp
)

add_executable(velocity_modulation_library_tests
    tests/VelocityModulationLibraryTests.cpp
)

add_executable(midi_buffer_transport_tests
    tests/MidiBufferTransportTests.cpp
    src/plugin/MidiBufferTransport.cpp
    src/plugin/MidiBufferTransport.h
)

target_link_libraries(sequencer_core_tests PRIVATE sequencer_core)
target_link_libraries(pattern_player_tests PRIVATE sequencer_core)
target_link_libraries(pattern_library_tests PRIVATE sequencer_core)
target_link_libraries(velocity_modulation_library_tests PRIVATE sequencer_core)
target_include_directories(midi_buffer_transport_tests PRIVATE src)
target_link_libraries(midi_buffer_transport_tests
    PRIVATE
        sequencer_core
        juce::juce_audio_basics
)

enable_testing()
add_test(NAME sequencer_core_tests COMMAND sequencer_core_tests)
add_test(NAME pattern_player_tests COMMAND pattern_player_tests)
add_test(NAME pattern_library_tests COMMAND pattern_library_tests)
add_test(NAME velocity_modulation_library_tests COMMAND velocity_modulation_library_tests)
add_test(NAME midi_buffer_transport_tests COMMAND midi_buffer_transport_tests)

juce_add_plugin(LivePatternSequencer
    COMPANY_NAME "Hew"
    BUNDLE_ID "com.hew.livepatternsequencer"
    PLUGIN_MANUFACTURER_CODE HewB
    PLUGIN_CODE Lpsq
    FORMATS VST3
    PRODUCT_NAME "Live Pattern Sequencer MVP"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(LivePatternSequencer PRIVATE
    src/plugin/MidiBufferTransport.cpp
    src/plugin/MidiBufferTransport.h
    src/plugin/PatternLibraryFileStore.cpp
    src/plugin/PatternLibraryFileStore.h
    src/plugin/VelocityModulationLibraryFileStore.cpp
    src/plugin/VelocityModulationLibraryFileStore.h
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

add_executable(plugin_processor_tests
    tests/PluginProcessorTests.cpp
)

target_include_directories(plugin_processor_tests PRIVATE src)
target_link_libraries(plugin_processor_tests
    PRIVATE
        LivePatternSequencer
        juce::juce_audio_utils
)
add_test(NAME plugin_processor_tests COMMAND plugin_processor_tests)
