cmake_minimum_required(VERSION 3.22)
project(openEDMDelay VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(OED_BUILD_PLUGIN "Build native plugins and editor tests" ON)
option(OED_BUILD_TESTS "Build tests" ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()
add_library(delay_core STATIC Source/Core/DelayEngine.cpp)
target_include_directories(delay_core PUBLIC Source/Core)
set_target_properties(delay_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(OED_BUILD_TESTS)
    enable_testing()
    add_executable(openedmdelay_tests Tests/DelayTests.cpp)
    target_link_libraries(openedmdelay_tests PRIVATE delay_core)
    add_test(NAME delay_core COMMAND openedmdelay_tests)
endif()
if(OED_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
    juce_add_binary_data(DelayAssets SOURCES assets/tape-echo.png)
    set_target_properties(DelayAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set(OED_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND OED_FORMATS AU)
    endif()
    juce_add_plugin(openEDMDelay
        COMPANY_NAME "Alan Marcero"
        COMPANY_WEBSITE "https://github.com/alanmarcero/openEDMDelay"
        COPYRIGHT "Copyright (c) 2026 Alan Marcero"
        BUNDLE_ID "com.alanmarcero.openedmdelay"
        PRODUCT_NAME "openEDMDelay"
        PLUGIN_MANUFACTURER_CODE Amrc PLUGIN_CODE Oedl
        FORMATS ${OED_FORMATS} VST3_CATEGORIES Fx Delay
        AU_MAIN_TYPE kAudioUnitType_Effect
        IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(openEDMDelay PRIVATE Source/Plugin/Parameters.cpp Source/Plugin/PluginProcessor.cpp Source/Plugin/PluginEditor.cpp Source/Plugin/DelayLookAndFeel.cpp)
    target_include_directories(openEDMDelay PUBLIC Source/Plugin Source/Core)
    target_compile_definitions(openEDMDelay PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(openEDMDelay PRIVATE delay_core DelayAssets juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(OED_BUILD_TESTS)
        add_executable(openedmdelay_plugin_tests Tests/PluginTests.cpp)
        target_link_libraries(openedmdelay_plugin_tests PRIVATE openEDMDelay delay_core DelayAssets juce::juce_audio_utils)
        add_test(NAME plugin_integration COMMAND openedmdelay_plugin_tests "${CMAKE_BINARY_DIR}/screenshots")
    endif()
endif()
