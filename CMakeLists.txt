cmake_minimum_required(VERSION 3.16)

project(AudioBridge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AUDIOBRIDGE_BUILD_GUI   "Build the Qt desktop application" ON)
option(AUDIOBRIDGE_BUILD_CLI   "Build the command line tool (device listing / headless routing)" ON)
option(AUDIOBRIDGE_BUILD_TESTS "Build the automated test suite" ON)
# Development only: lets the GUI run on non-Windows systems against simulated devices.
# Never enabled for Windows builds - the Windows application always uses WASAPI.
option(AUDIOBRIDGE_DEV_SIMULATION "Non-Windows development build with simulated devices" OFF)

if(MINGW)
    # Self-contained executables: no libstdc++/libgcc/winpthread DLLs to ship.
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /wd4100)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    if(WIN32)
        add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    endif()
endif()

# ---------------------------------------------------------------------------------
# Core: platform independent audio engine (C++17, no dependencies)
add_library(audiobridge_core STATIC
    src/core/AudioTypes.cpp
    src/core/SampleConvert.cpp
    src/core/BroadcastRing.cpp
    src/core/AudioResampler.cpp
    src/core/AudioProcessing.cpp
    src/core/AudioInputProcessor.cpp
    src/core/AudioMixer.cpp
    src/core/AudioRoutingMatrix.cpp
    src/core/AudioEngine.cpp
)
target_include_directories(audiobridge_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(audiobridge_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------------
# Windows backend: WASAPI + Core Audio device enumeration
if(WIN32)
    add_library(audiobridge_wasapi STATIC
        src/backend/wasapi/WasapiCommon.cpp
        src/backend/wasapi/WasapiStream.cpp
        src/backend/wasapi/WasapiBackend.cpp
    )
    target_link_libraries(audiobridge_wasapi PUBLIC audiobridge_core ole32 oleaut32 avrt uuid)
    set(AUDIOBRIDGE_PLATFORM_BACKEND audiobridge_wasapi)
endif()

# ---------------------------------------------------------------------------------
# Simulated backend: tests and non-Windows development builds only
add_library(audiobridge_sim STATIC src/backend/sim/SimulatedBackend.cpp)
target_link_libraries(audiobridge_sim PUBLIC audiobridge_core)

if(NOT WIN32)
    # On non-Windows hosts the tools run against simulated devices (development only).
    set(AUDIOBRIDGE_PLATFORM_BACKEND audiobridge_sim)
endif()

# ---------------------------------------------------------------------------------
# Command line tool
if(AUDIOBRIDGE_BUILD_CLI)
    add_executable(AudioBridgeCLI src/cli/main.cpp)
    target_link_libraries(AudioBridgeCLI PRIVATE ${AUDIOBRIDGE_PLATFORM_BACKEND})
    install(TARGETS AudioBridgeCLI RUNTIME DESTINATION .)
endif()

# ---------------------------------------------------------------------------------
# Qt desktop application
if(AUDIOBRIDGE_BUILD_GUI)
    if(NOT WIN32 AND NOT AUDIOBRIDGE_DEV_SIMULATION)
        message(STATUS "GUI skipped: on non-Windows hosts it requires -DAUDIOBRIDGE_DEV_SIMULATION=ON")
    else()
        find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
        # Cross-compilation: the target Qt's moc/uic/rcc are Windows executables.
        # Point AUDIOBRIDGE_QT_HOST_TOOLS at a host Qt 5.15 bin directory instead
        # (all Qt 5.15.x generators emit the same output revision).
        set(AUDIOBRIDGE_QT_HOST_TOOLS "" CACHE PATH "Directory with host moc/uic/rcc (cross builds only)")
        if(AUDIOBRIDGE_QT_HOST_TOOLS)
            foreach(tool moc uic rcc)
                set_target_properties(Qt5::${tool} PROPERTIES IMPORTED_LOCATION ${AUDIOBRIDGE_QT_HOST_TOOLS}/${tool})
            endforeach()
        endif()
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTOUIC ON)
        set(CMAKE_AUTORCC ON)
        set(APP_SOURCES
            src/app/main.cpp
            src/app/Theme.cpp
            src/app/AppSettings.cpp
            src/app/EngineController.cpp
            src/app/MainWindow.cpp
            src/app/MeterHub.h
            src/app/widgets/LevelMeter.cpp
            src/app/widgets/ToggleSwitch.cpp
            src/app/widgets/RoutingMatrixWidget.cpp
            src/app/widgets/RoutingDiagramWidget.cpp
            src/app/pages/PageCommon.cpp
            src/app/pages/DashboardPage.cpp
            src/app/pages/InputsPage.cpp
            src/app/pages/OutputsPage.cpp
            src/app/pages/RoutingPage.cpp
            src/app/pages/MixerPage.cpp
            src/app/pages/SettingsPage.cpp
            src/app/pages/SettingsPage.ui
            src/app/resources/resources.qrc
        )
        if(WIN32)
            configure_file(src/app/resources/AudioBridge.rc.in ${CMAKE_CURRENT_BINARY_DIR}/AudioBridge.rc @ONLY)
            list(APPEND APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/AudioBridge.rc)
        endif()
        add_executable(AudioBridge WIN32 ${APP_SOURCES})
        set_target_properties(AudioBridge PROPERTIES AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/pages)
        target_include_directories(AudioBridge PRIVATE src/app)
        target_compile_definitions(AudioBridge PRIVATE AUDIOBRIDGE_VERSION="${PROJECT_VERSION}")
        if(NOT WIN32)
            target_compile_definitions(AudioBridge PRIVATE AUDIOBRIDGE_DEV_SIMULATION)
        endif()
        target_link_libraries(AudioBridge PRIVATE ${AUDIOBRIDGE_PLATFORM_BACKEND} Qt5::Widgets)
        install(TARGETS AudioBridge RUNTIME DESTINATION .)
    endif()
endif()

# ---------------------------------------------------------------------------------
# Tests
if(AUDIOBRIDGE_BUILD_TESTS)
    enable_testing()
    add_executable(audiobridge_tests
        tests/test_main.cpp
        tests/test_dsp.cpp
        tests/test_engine.cpp
    )
    target_include_directories(audiobridge_tests PRIVATE tests)
    target_link_libraries(audiobridge_tests PRIVATE audiobridge_sim)
    add_test(NAME dsp_units COMMAND audiobridge_tests resampler)
    foreach(t sample_format channel_mapping broadcast_ring noise_suppressor high_pass noise_gate limiter
              device_name routing_matrix scenario engine_ threaded)
        add_test(NAME ${t} COMMAND audiobridge_tests ${t})
    endforeach()
endif()
