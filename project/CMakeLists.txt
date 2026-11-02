# beatles - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(beatles LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(BEATLES_SOURCES
    src/main.cpp
    src/xam_input_hooks.cpp
    src/net_hooks.cpp
    src/shm_cleanup.cpp
    src/debug_probe.cpp
    src/perf_hooks.cpp
    src/present_hooks.cpp
    src/wait_profiler.cpp
    src/hitch_tracer.cpp
    src/gpu_capture.cpp
    src/renderdoc_guard.cpp
    src/world_rate_hooks.cpp
    src/gpu_frame_log.cpp
)

if(WIN32)
    add_executable(beatles WIN32 ${BEATLES_SOURCES})
else()
    add_executable(beatles ${BEATLES_SOURCES})
endif()

rexglue_setup_target(beatles GPU_PLUGINS xenos)
