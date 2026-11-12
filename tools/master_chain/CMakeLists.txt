cmake_minimum_required(VERSION 3.22)
project(BlackboxMaster VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# DSP core: plain C, shared by the plugin, the offline CLI and the firmware port.
add_library(mc_dsp STATIC src/dsp/mc_dsp.c src/dsp/mc_clr.c src/dsp/mc_cmp.c src/dsp/mc_max.c)
target_include_directories(mc_dsp PUBLIC src/dsp)
if(NOT MSVC)
    target_link_libraries(mc_dsp PUBLIC m)
    target_compile_options(mc_dsp PRIVATE -Wall -Wextra)
endif()

add_executable(mc_proto src/mc_proto.c src/wavio.c)
target_include_directories(mc_proto PRIVATE src)
target_link_libraries(mc_proto PRIVATE mc_dsp)

# Double-precision reference: the unmodified Airwindows classes (reference/airwindows, MIT),
# compiled against a tiny VST2 shim so the float ports can be checked against them.
set(AW reference/airwindows)
add_executable(ref_render reference/ref_render.cpp reference/ref_m2.cpp reference/ref_ps.cpp src/wavio.c
    ${AW}/Mastering2/Mastering2.cpp ${AW}/Mastering2/Mastering2Proc.cpp
    ${AW}/PurestSquish/PurestSquish.cpp ${AW}/PurestSquish/PurestSquishProc.cpp)
target_include_directories(ref_render PRIVATE reference/shim)
set_source_files_properties(reference/ref_m2.cpp ${AW}/Mastering2/Mastering2.cpp ${AW}/Mastering2/Mastering2Proc.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/${AW}/Mastering2"
               COMPILE_DEFINITIONS "createEffectInstance=createEffectInstance_m2")
set_source_files_properties(reference/ref_ps.cpp ${AW}/PurestSquish/PurestSquish.cpp ${AW}/PurestSquish/PurestSquishProc.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/${AW}/PurestSquish"
               COMPILE_DEFINITIONS "createEffectInstance=createEffectInstance_ps")
if(NOT MSVC)
    set_source_files_properties(${AW}/Mastering2/Mastering2.cpp ${AW}/Mastering2/Mastering2Proc.cpp
        ${AW}/PurestSquish/PurestSquish.cpp ${AW}/PurestSquish/PurestSquishProc.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Plugin (VST3 / AU / Standalone). Reuse the JUCE already fetched into build/_deps when present
# so configuring works offline.
option(MC_BUILD_PLUGIN "Build the JUCE plugin" ON)
if(MC_BUILD_PLUGIN)
    include(FetchContent)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/_deps/juce-src/CMakeLists.txt")
        set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/build/_deps/juce-src")
    endif()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)

    juce_add_plugin(BlackboxMaster
        PRODUCT_NAME "Blackbox Master"
        COMPANY_NAME "Blindsmyth"
        PLUGIN_MANUFACTURER_CODE Bbsm
        PLUGIN_CODE Bbmc
        FORMATS Standalone VST3 AU
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE)

    target_sources(BlackboxMaster PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp)
    target_compile_definitions(BlackboxMaster PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(BlackboxMaster
        PRIVATE mc_dsp juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # Renders every page to PNG with the demo loop running (layout and meter check, no clicking).
    juce_add_console_app(ui_snapshot PRODUCT_NAME "ui_snapshot")
    target_sources(ui_snapshot PRIVATE src/tools/ui_snapshot.cpp
        src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp)
    target_compile_definitions(ui_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ui_snapshot PRIVATE mc_dsp juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
