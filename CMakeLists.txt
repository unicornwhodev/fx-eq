cmake_minimum_required(VERSION 3.22)

project(MusiqueEQ VERSION 0.1.0 LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(MusiqueEQ
    COMPANY_NAME "Musique"
    BUNDLE_ID "com.musique.eq"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE MQFX
    PLUGIN_CODE MEQ1
    FORMATS Standalone VST3
    PRODUCT_NAME "Musique EQ and Filter"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/icon_big.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/icon_small.png"
)

juce_generate_juce_header(MusiqueEQ)

target_sources(MusiqueEQ PRIVATE
    Source/EQGraphMapping.h
    Source/EQGraphUI.h
    Source/EQPresetMigration.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
)

target_compile_definitions(MusiqueEQ PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

if (MSVC)
    target_compile_options(MusiqueEQ PRIVATE
        $<$<CONFIG:Release>:/O2 /fp:fast /GL>
    )
    target_link_options(MusiqueEQ PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()

juce_add_binary_data(MusiqueEQAssets
    SOURCES
        Source/Assets/icon_big.png
        Source/Assets/icon_small.png
        Source/Assets/favicon.png
        Source/Assets/logo.png
)

target_link_libraries(MusiqueEQ PRIVATE
    MusiqueEQAssets
    FXShared
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_recommended_lto_flags
)

juce_add_console_app(MusiqueEQDSPTests
    PRODUCT_NAME "Musique EQ DSP Tests"
)

juce_generate_juce_header(MusiqueEQDSPTests)

target_sources(MusiqueEQDSPTests PRIVATE
    Tests/EQProcessorTests.cpp
    Source/EQGraphMapping.h
    Source/EQGraphUI.h
    Source/EQPresetMigration.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
)

target_compile_definitions(MusiqueEQDSPTests PRIVATE
    MUSIQUE_EQ_DSP_TESTS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_include_directories(MusiqueEQDSPTests PRIVATE
    Source
)

target_link_libraries(MusiqueEQDSPTests PRIVATE
    FXShared
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

add_test(NAME MusiqueEQDSPTests COMMAND MusiqueEQDSPTests)
