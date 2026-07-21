cmake_minimum_required(VERSION 3.22)

project(JeDExSymbiosis VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(JeDExSymbiosis
    PRODUCT_NAME                "JeDEx Symbiosis"
    COMPANY_NAME                "JeDEx Audio"
    BUNDLE_ID                   com.jedexaudio.symbiosis
    PLUGIN_MANUFACTURER_CODE    Jdex
    PLUGIN_CODE                 Jsym
    FORMATS                     VST3
    VST3_CATEGORIES             Fx Dynamics
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
)

target_sources(JeDExSymbiosis PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(JeDExSymbiosis PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(JeDExSymbiosis
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
