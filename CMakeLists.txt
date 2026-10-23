cmake_minimum_required(VERSION 3.20)
project(LiteralUniverse VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Disney Universe is a 32-bit game. Configure with -A Win32.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(LiteralUniverse SHARED
  src/main.cpp
  src/proxy.cpp
  src/config.cpp
  src/log.cpp
  src/iat.cpp
  src/display.cpp
  src/game_patches.cpp
  src/audio_hooks.cpp
  src/call_trace.cpp
  src/d3d9_hooks.cpp
  src/frame_stats.cpp
  src/d3d9_vtbl.c
  src/device_scan.cpp
  src/diag_tools.cpp
  src/input_hooks.cpp
  src/input_vtbl.c
  src/dinput8.def
  src/version.rc
)

set_target_properties(LiteralUniverse PROPERTIES OUTPUT_NAME dinput8)
target_compile_definitions(LiteralUniverse PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
  LU_VERSION="${PROJECT_VERSION}")
target_compile_options(LiteralUniverse PRIVATE /W4 /utf-8)
target_link_libraries(LiteralUniverse PRIVATE user32 shcore dwmapi winmm setupapi dxguid ole32)
