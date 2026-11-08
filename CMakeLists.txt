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
# Simulated backend: tests and non-Windows development builds only
add_library(audiobridge_sim STATIC src/backend/sim/SimulatedBackend.cpp)
target_link_libraries(audiobridge_sim PUBLIC audiobridge_core)

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
