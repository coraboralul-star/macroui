cmake_minimum_required(VERSION 3.21)

project(macroui-engine LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "macroui-engine is x64 only")
endif()

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# One output path for the single-config and multi-config generators.
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${cfg}" cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}")
  endforeach()
else()
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

add_library(engine_core STATIC src/core/version.c)
target_include_directories(engine_core PUBLIC src/core)

add_executable(macroui-engine src/main.c)
target_link_libraries(macroui-engine PRIVATE engine_core)

if(WIN32)
  target_sources(macroui-engine PRIVATE src/win/win_entry.c)
endif()
