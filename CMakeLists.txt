cmake_minimum_required(VERSION 3.22)

project(StereoDelayHall VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is fetched at configure time. Pin a different tag if needed, e.g.:
#   cmake -B build -DJUCE_GIT_TAG=8.0.4
set(JUCE_GIT_TAG "8.0.4" CACHE STRING "JUCE git tag to fetch")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${JUCE_GIT_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# AU (Audio Unit) is the native macOS format; JUCE enables it automatically on macOS only.
juce_add_plugin(StereoDelayHall
    COMPANY_NAME            "Vibe Audio"
    BUNDLE_ID               "com.vibeaudio.stereodelayhall"
    PLUGIN_MANUFACTURER_CODE Vibe
    PLUGIN_CODE             Sdh1
    FORMATS                 AU VST3 Standalone
    PLUGIN_NAME             "essentia"
    PRODUCT_NAME            "StereoDelayHall"
    DESCRIPTION             "Stereo delay with independent L/R times, modulation, and a hall reverb on the repeats"
    VST3_CATEGORIES         "Delay Reverb Stereo"
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_SYNTH                FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(StereoDelayHall PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/DelayHallEngine.cpp
)

target_include_directories(StereoDelayHall PRIVATE Source)

target_compile_definitions(StereoDelayHall PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
)

# silence warnings coming from the bundled Steinberg VST3 SDK headers
target_compile_options(StereoDelayHall PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wno-nontrivial-memcall>
)

target_link_libraries(StereoDelayHall PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Standalone DSP unit test (no JUCE needed):
#   cmake --build build --target dsp_test && ./build/dsp_test
option(STEREODELAYHALL_BUILD_TESTS "Build the DSP unit test" ON)
if(STEREODELAYHALL_BUILD_TESTS)
    add_executable(dsp_test
        tests/dsp_test.cpp
        Source/DSP/DelayHallEngine.cpp
    )
    target_include_directories(dsp_test PRIVATE Source)
    target_compile_features(dsp_test PRIVATE cxx_std_17)
endif()
