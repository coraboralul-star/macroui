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
  add_compile_options(/W4 /experimental:c11atomics)
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

add_library(engine_core STATIC
  src/core/version.c
  src/core/clock.c
  src/core/hook.c
  src/core/output.c
  src/core/process.c
  src/core/runner.c
  src/core/json.c
  src/core/profile.c
  src/ipc/report.c)
target_include_directories(engine_core PUBLIC src/core src/ipc)
if(NOT MSVC)
  target_link_libraries(engine_core PUBLIC m)
endif()

add_executable(macroui-engine src/main.c)
target_link_libraries(macroui-engine PRIVATE engine_core)

add_executable(engine-clock-test tests/clock_test.c)
target_link_libraries(engine-clock-test PRIVATE engine_core)

add_executable(engine-hook-test tests/hook_test.c)
target_link_libraries(engine-hook-test PRIVATE engine_core)

add_executable(engine-output-test tests/output_test.c)
target_link_libraries(engine-output-test PRIVATE engine_core)

add_executable(engine-runner-test tests/runner_test.c)
target_link_libraries(engine-runner-test PRIVATE engine_core)

add_executable(engine-trace bench/trace.c)
target_link_libraries(engine-trace PRIVATE engine_core)

add_executable(engine-process-test tests/process_test.c)
target_link_libraries(engine-process-test PRIVATE engine_core)

add_executable(engine-profile-test tests/profile_test.c)
target_link_libraries(engine-profile-test PRIVATE engine_core)

if(WIN32)
  add_library(engine_win STATIC src/win/win_entry.c src/win/clock_win.c src/win/hook_win.c src/win/output_win.c)
  target_include_directories(engine_win PUBLIC src/win)
  target_link_libraries(engine_win PUBLIC engine_core user32 shell32)
  target_link_libraries(macroui-engine PRIVATE engine_win)
  if(MSVC)
    target_link_options(macroui-engine PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
  else()
    target_link_options(macroui-engine PRIVATE "-mwindows")
  endif()

  add_executable(engine-clock-wait-test tests/clock_wait_test.c)
  target_link_libraries(engine-clock-wait-test PRIVATE engine_win)

  add_executable(engine-hook-win-test tests/hook_win_test.c)
  target_link_libraries(engine-hook-win-test PRIVATE engine_win)

  add_executable(engine-output-win-test tests/output_win_test.c)
  target_link_libraries(engine-output-win-test PRIVATE engine_win)

  add_executable(engine-process-win-test tests/process_win_test.c)
  target_link_libraries(engine-process-win-test PRIVATE engine_win)
endif()
