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

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "macOS deployment target" FORCE)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures" FORCE)

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

    # ── Patch: remove "Audio input is muted" notification bar ────────────
    # DAWalka is an offline generator — no real-time audio input, so the
    # feedback-loop banner added by JUCE's Standalone window is irrelevant.
    set(JUCE_STANDALONE_H "${JUCE_SOURCE_DIR}/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h")
    if(EXISTS "${JUCE_STANDALONE_H}")
        file(READ "${JUCE_STANDALONE_H}" JUCE_SRC)

        # 1. Comment out addChildComponent(notification)
        string(REPLACE "            addChildComponent (notification);"
                       "            // DAWalka: notification area removed\n            // addChildComponent (notification);"
                       JUCE_SRC "${JUCE_SRC}")

        # 2. Force shouldShowNotification = false in constructor block
        string(REPLACE "                shouldShowNotification = inputMutedValue.getValue();"
                       "                shouldShowNotification = false; // DAWalka: never show"
                       JUCE_SRC "${JUCE_SRC}")

        # 3. Call inputMutedChanged(false) instead of (shouldShowNotification)
        string(REPLACE "            inputMutedChanged (shouldShowNotification);"
                       "            inputMutedChanged (false); // DAWalka: never show"
                       JUCE_SRC "${JUCE_SRC}")

        # 4. Patch inputMutedChanged to always suppress the banner
        string(REPLACE "            shouldShowNotification = newInputMutedValue;\n            notification.setVisible (shouldShowNotification);"
                       "            // DAWalka: notification area disabled\n            shouldShowNotification = false;"
                       JUCE_SRC "${JUCE_SRC}")

        # 5. Remove extraHeight in inputMutedChanged
        string(REPLACE "                const int extraHeight = shouldShowNotification ? NotificationArea::height : 0;\n                const auto rect = getSizeToContainEditor();\n                setSize (rect.getWidth(), rect.getHeight() + extraHeight);"
                        "                const auto rect = getSizeToContainEditor();\n                setSize (rect.getWidth(), rect.getHeight());"
                        JUCE_SRC "${JUCE_SRC}")

        # 6. Remove "Options" button from standalone title bar
        string(REPLACE "        Component::addAndMakeVisible (optionsButton);"
                        "        // DAWalka: Options button removed\n        // Component::addAndMakeVisible (optionsButton);"
                        JUCE_SRC "${JUCE_SRC}")

        string(REPLACE "        optionsButton.addListener (this);"
                        "        // DAWalka: Options button removed\n        // optionsButton.addListener (this);"
                        JUCE_SRC "${JUCE_SRC}")

        string(REPLACE "        optionsButton.setTriggeredOnMouseDown (true);"
                        "        // DAWalka: Options button removed\n        // optionsButton.setTriggeredOnMouseDown (true);"
                        JUCE_SRC "${JUCE_SRC}")

        string(REPLACE "        optionsButton.setBounds (8, 6, 60, getTitleBarHeight() - 8);"
                        "        // DAWalka: Options button removed\n        // optionsButton.setBounds (8, 6, 60, getTitleBarHeight() - 8);"
                        JUCE_SRC "${JUCE_SRC}")

        file(WRITE "${JUCE_STANDALONE_H}" "${JUCE_SRC}")
        message(STATUS "DAWalka: patched JUCE standalone window — notification bar and Options button removed")
    endif()
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
    FORMATS                    AU VST3 Standalone
    PRODUCT_NAME               "DAWalka"
    BUNDLE_ID                  "com.dawalka.audio"
    PLUGIN_MANUFACTURER_CODE   "DWLK"
    PLUGIN_CODE                "DWLK"
    COPY_PLUGIN_AFTER_BUILD    TRUE
    VST3_COPY_DIR              "${CMAKE_BINARY_DIR}/VST3"
    AU_COPY_DIR                "${CMAKE_BINARY_DIR}/AU"
    PLUGIN_COPY_DIR            "${CMAKE_BINARY_DIR}/Standalone"
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
target_compile_features(DAWalka             PRIVATE cxx_std_17 cxx_std_20)
target_compile_features(DAWalka_AU          PRIVATE cxx_std_17 cxx_std_20)
target_compile_features(DAWalka_VST3        PRIVATE cxx_std_17 cxx_std_20)
target_compile_features(DAWalka_Standalone  PRIVATE cxx_std_17 cxx_std_20)

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
    Source/Common.mm
    Source/DragnDropSource.cpp
    Source/ClipboardHelper.mm)

# PluginEditor.cpp must be compiled separately per format because it uses
# JucePlugin_Build_Standalone to conditionally show/hide the "Use Project BPM"
# toggle.  The shared DAWalka target compiles all sources with Standalone=1,
# which would hide the toggle in AU/VST3 as well.  By compiling PluginEditor.cpp
# into separate static libraries with explicit defines, each format gets the
# correct behaviour: AU/VST3 show the toggle, standalone hides it.
add_library(DAWalka_PluginEditor STATIC
    Source/PluginEditor.cpp)
target_link_libraries(DAWalka_PluginEditor PRIVATE
    DAWalkaAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra)

add_library(DAWalka_StandaloneEditor STATIC
    Source/PluginEditor.cpp)
target_compile_definitions(DAWalka_StandaloneEditor PRIVATE IS_STANDALONE_BUILD=1)
target_link_libraries(DAWalka_StandaloneEditor PRIVATE
    DAWalkaAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra)

target_link_libraries(DAWalka_AU    PRIVATE DAWalka_PluginEditor)
target_link_libraries(DAWalka_VST3  PRIVATE DAWalka_PluginEditor)
target_link_libraries(DAWalka_Standalone PRIVATE DAWalka_StandaloneEditor)

# Custom Standalone entrypoint so we can call setUsingNativeTitleBar(true)
# before the window is shown (JUCE draws its own title bar otherwise).
target_sources(DAWalka_Standalone PRIVATE Source/StandaloneMain.cpp)
target_compile_definitions(DAWalka_Standalone PRIVATE
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_ENTRYPOINT=1)
target_link_libraries(DAWalka_Standalone PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_utils)

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

# Standalone: embed into the artefacts bundle (where JUCE builds it), then
# copy to the final location.  This avoids a race with JUCE's own copy step.
add_custom_command(TARGET DAWalka_Standalone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/DAWalka_artefacts/Standalone/DAWalka.app/Contents/Resources"
    COMMAND sh -c
        "rsync -a --exclude='venv' --exclude='__pycache__' --exclude='*.pyc' \
                --exclude='vendor/*/.git' --exclude='vendor/*/build' \
                '${CMAKE_SOURCE_DIR}/python_backend/' \
                '${CMAKE_BINARY_DIR}/DAWalka_artefacts/Standalone/DAWalka.app/Contents/Resources/python_backend/'"
    COMMAND sh -c
        "rm -rf '${CMAKE_BINARY_DIR}/DAWalka_artefacts/Standalone/DAWalka.app/Contents/MacOS/DAWalka Launcher.app' \
          && cp -R '${CMAKE_SOURCE_DIR}/launcher/DAWalka Launcher.app' \
                 '${CMAKE_BINARY_DIR}/DAWalka_artefacts/Standalone/DAWalka.app/Contents/MacOS/'"
    COMMAND /usr/bin/codesign --force --deep --sign -
        "${CMAKE_BINARY_DIR}/DAWalka_artefacts/Standalone/DAWalka.app"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/Standalone"
    COMMAND sh -c
        "rm -rf '${CMAKE_BINARY_DIR}/Standalone/DAWalka.app' \
          && cp -R '${CMAKE_BINARY_DIR}/DAWalka_artefacts/Standalone/DAWalka.app' \
                 '${CMAKE_BINARY_DIR}/Standalone/'"
    COMMAND /usr/bin/codesign --force --deep --sign -
        "${CMAKE_BINARY_DIR}/Standalone/DAWalka.app"
    COMMENT "Embedding Python backend into DAWalka_Standalone bundle (skipping venv)"
    VERBATIM)

# ─── Standalone app icon (.icns from icon.png) ──────────────────────
add_custom_command(TARGET DAWalka_Standalone POST_BUILD
    COMMAND sh -c "ICONSET=\$(mktemp -d)/AppIcon.iconset && mkdir -p \"\$ICONSET\" && sips -z 16 16 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_16x16.png\" >/dev/null && sips -z 32 32 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_16x16@2x.png\" >/dev/null && sips -z 32 32 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_32x32.png\" >/dev/null && sips -z 64 64 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_32x32@2x.png\" >/dev/null && sips -z 128 128 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_128x128.png\" >/dev/null && sips -z 256 256 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_128x128@2x.png\" >/dev/null && sips -z 256 256 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_256x256.png\" >/dev/null && sips -z 512 512 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_256x256@2x.png\" >/dev/null && sips -z 512 512 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_512x512.png\" >/dev/null && sips -z 1024 1024 '${CMAKE_SOURCE_DIR}/Resources/icon_512.png' --out \"\$ICONSET/icon_512x512@2x.png\" >/dev/null && iconutil -c icns \"\$ICONSET\" -o '${CMAKE_BINARY_DIR}/Standalone/DAWalka.app/Contents/Resources/AppIcon.icns' 2>/dev/null && defaults write '${CMAKE_BINARY_DIR}/Standalone/DAWalka.app/Contents/Info.plist' CFBundleIconFile AppIcon && rm -rf \"\$(dirname \$ICONSET)\""
    COMMENT "Building AppIcon.icns for standalone bundle"
    VERBATIM)

install(TARGETS DAWalka
    BUNDLE  DESTINATION "Library/Audio/Plug-Ins/Components"
    RUNTIME DESTINATION bin)

install(TARGETS DAWalka_Standalone
    BUNDLE DESTINATION "/Applications")
