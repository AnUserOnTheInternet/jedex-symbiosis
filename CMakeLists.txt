cmake_minimum_required(VERSION 3.22)

project(Carve VERSION 1.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS: build one universal binary that runs native on both Apple Silicon and Intel,
# targeting an OS old enough to cover the installed base. Set before JUCE is fetched so
# the framework is built for the same architectures.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# Audio Unit is what Logic Pro and GarageBand load, and it only exists on Apple. Everyone
# gets VST3.
if(APPLE)
    set(CARVE_FORMATS AU VST3)
else()
    set(CARVE_FORMATS VST3)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(CarveAssets
    SOURCES
        assets/jedex_logo.png
        assets/bigice_logo.png
)

juce_add_plugin(Carve
    PRODUCT_NAME                "CARVE"
    COMPANY_NAME                "JeDEx Audio"
    BUNDLE_ID                   com.jedexaudio.carve
    PLUGIN_MANUFACTURER_CODE    Jdex
    PLUGIN_CODE                 Crve
    FORMATS                     ${CARVE_FORMATS}
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
)

target_sources(Carve PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(Carve PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Carve
    PRIVATE
        CarveAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
