cmake_minimum_required(VERSION 3.22)

# Pin to the CMake 3.x policy behaviour even when running under CMake 4.x.
# CMake 4 made a few policy defaults stricter which occasionally confuses
# older JUCE/CMakeLists.txt fragments.  Setting the policy version makes
# behaviour deterministic.
if(POLICY CMP0075)
    cmake_policy(SET CMP0075 NEW)
endif()
if(POLICY CMP0095)
    cmake_policy(SET CMP0095 NEW)
endif()
if(POLICY CMP0128)
    cmake_policy(SET CMP0128 NEW)
endif()
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_POLICY_DEFAULT_CMP0075 NEW CACHE INTERNAL "")

project(DAWalka
    VERSION 1.0.0
    DESCRIPTION "AI Audio Generator for Logic Pro 12"
    # Enable both C and C++: C is needed because JUCE's vendored modules
    # (and the AUv2 wrapper) include a small number of C sources, and CMake
    # would otherwise abort at generate time with
    # "Missing variable: CMAKE_C_COMPILE_OBJECT".
    LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT APPLE)
    message(FATAL_ERROR "DAWalka supports macOS only")
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "macOS deployment target")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures")

# JUCE 8 — pulled via FetchContent (or override JUCE_DIR to use local install)
set(JUCE_DIR "" CACHE PATH "Path to local JUCE 8 install (leave empty to fetch)")

if(JUCE_DIR STREQUAL "")
    include(FetchContent)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_HELPER_TOOLS OFF CACHE BOOL "" FORCE)
    set(JUCE_ENABLE_MODULE_SOURCE_GLOBAL_HEADER OFF CACHE BOOL "" FORCE)
    set(JUCE_INSTALL_DESTINATION "${CMAKE_BINARY_DIR}/juce_install" CACHE PATH "" FORCE)
    set(JUCE_BUILD_SHARED_LIBRARIES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.8
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
else()
    add_subdirectory(${JUCE_DIR} JUCE_build)
endif()

juce_add_plugin(DAWalka
    COMPANY_NAME               "DAWalka"
    IS_SYNTH                   FALSE
    IS_MIDI_EFFECT             FALSE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_AU_PLUGIN_TYPE          GENERATOR
    AU_MAIN_TYPE               kAudioUnitType_Generator     # "augn" — offline generator
    # DAWalka is fundamentally an OFFLINE generator: the user enters a prompt,
    # the python backend renders audio to a file, the file is loaded back
    # into the plugin and played back.  An `augn` (Generator) AU is the
    # correct type — it doesn't require MIDI input, doesn't try to be a
    # real-time synth, and the host treats it as a "source" that produces
    # audio regions.  This also sidesteps the auval MusicDeviceMIDIEventList
    # failure we saw with the MusicDevice ("aumu") type.
    # DAWalka spawns a child python process and does network IO — by definition
    # NOT sandbox-safe. Logic Pro 11 will SIGSYS the plugin if it claims to be
    # sandbox-safe and then tries to fork() / network.
    AU_SANDBOX_SAFE            FALSE
    FORMATS                    AU VST3
    PRODUCT_NAME               "DAWalka"
    BUNDLE_ID                  "com.dawalka.audio"
    PLUGIN_MANUFACTURER_CODE   "DWLK"
    PLUGIN_CODE                "DWLK"
    COPY_PLUGIN_AFTER_BUILD    TRUE
    VST3_COPY_DIR              "${CMAKE_BINARY_DIR}/VST3"
    AU_COPY_DIR                "${CMAKE_BINARY_DIR}/AU"
    ENABLE_AU_FACTORY_VIEW     FALSE
    ENABLE_CUSTOM_PLUGIN_FORMAT FALSE
    PLUGIN_COPY_DIR            "${CMAKE_BINARY_DIR}/Plugins"
    USE_BACKGROUND_THREAD     FALSE
    ICON_BIG_NAME              icon_512
    ICON_SMALL_NAME            icon_256
    SUPPORTS_XPCA              FALSE
    SUPPORTS_CLAP              FALSE
    SUPPORTS_AU                TRUE
    SUPPORTS_STANDALONE        TRUE
    SUPPORTS_VST3              TRUE
    SUPPORTS_LV2               FALSE
    SUPPORTS_AAX               FALSE
    SUPPORTS_ARA               FALSE)

target_compile_definitions(DAWalka
    PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_options(DAWalka PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -Wno-deprecated-declarations)
# NOTE: do NOT add -fobjc-arc here. JUCE's .mm files in _deps use manual
# retain/release (MRR) and would fail to compile under ARC. Our own code
# doesn't contain any Objective-C++, so this is a non-issue.

# JUCE 8's AU wrapper uses `if constexpr`, which requires C++17. CMake
# inherits CMAKE_CXX_STANDARD, but the juce_add_plugin target may not always
# pick it up. Setting it explicitly on both targets guarantees the standard.
target_compile_features(DAWalka    PRIVATE cxx_std_17 cxx_std_20)
target_compile_features(DAWalka_AU PRIVATE cxx_std_17 cxx_std_20)
target_compile_features(DAWalka_VST3 PRIVATE cxx_std_17 cxx_std_20)

juce_add_binary_data(DAWalkaAssets
    SOURCES
        Resources/icon_512.png
        Resources/icon_256.png)

target_link_libraries(DAWalka PRIVATE
    DAWalkaAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_cryptography
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra)

target_sources(DAWalka PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ModelManager.cpp
    Source/BackendClient.mm
    Source/PromptBuilder.cpp
    Source/GenerationHistory.cpp
    Source/WaveformComponent.cpp
    Source/ModelSelectorComponent.cpp
    Source/HistoryComponent.cpp
    Source/FileListComponent.cpp
    Source/ProgressOverlay.cpp
    Source/AppleSiliconDetector.cpp
    Source/AudioExporter.cpp
    Source/LookAndFeel.cpp
    Source/Common.cpp
    Source/DragnDropSource.cpp
    Source/ClipboardHelper.mm)

# ─── Embed Python backend into plugin bundles ────────────────────────────
# JUCE wraps the DAWalka executable into plugin bundles and copies them to
# the configured copy dirs.  We add POST_BUILD steps to the format targets
# that run *after* JUCE's own copy step and embed the python_backend directory
# into Contents/Resources/.
# We use a hard-coded path here because $<TARGET_BUNDLE_CONTENT_DIR:...>
# is rejected by CMake for non-bundle targets (and the DAWalka target is the
# underlying Mach-O executable, not the plugin bundle itself).
# We explicitly skip the per-machine venv and __pycache__ folders so the
# bundle stays small (≈ 1 MB) — the venv is rebuilt at install time
# by the DAWalka Launcher.
function(dawalka_embed_runtime target_name bundle_dir)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${bundle_dir}/Contents/Resources"
        COMMAND sh -c
            "rsync -a --exclude='venv' --exclude='__pycache__' --exclude='*.pyc' \
                    --exclude='vendor/*/.git' --exclude='vendor/*/build' \
                    '${CMAKE_SOURCE_DIR}/python_backend/' \
                    '${bundle_dir}/Contents/Resources/python_backend/'"
        COMMAND sh -c
            "rm -rf '${bundle_dir}/Contents/MacOS/DAWalka Launcher.app' \
              && cp -R '${CMAKE_SOURCE_DIR}/launcher/DAWalka Launcher.app' \
                     '${bundle_dir}/Contents/MacOS/'"
        COMMAND /usr/bin/codesign --force --deep --sign - "${bundle_dir}"
        COMMENT "Embedding Python backend into ${target_name} bundle (skipping venv)"
        VERBATIM)
endfunction()

dawalka_embed_runtime(DAWalka_AU "${CMAKE_BINARY_DIR}/AU/DAWalka.component")
dawalka_embed_runtime(DAWalka_VST3 "${CMAKE_BINARY_DIR}/VST3/DAWalka.vst3")

install(TARGETS DAWalka
    BUNDLE  DESTINATION "Library/Audio/Plug-Ins/Components"
    RUNTIME DESTINATION bin)
