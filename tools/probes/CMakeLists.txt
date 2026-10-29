cmake_minimum_required(VERSION 3.16)
project(move_probe C)
add_executable(move-probe move_probe.c)
target_link_libraries(move-probe PRIVATE user32 winmm)
