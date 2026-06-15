cmake_minimum_required(VERSION 3.21)
project(win_shims LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build per-DLL targets with explicit output names so JNA finds them by base name.

add_library(Ole32 SHARED ole32_shim.cpp)
set_target_properties(Ole32 PROPERTIES OUTPUT_NAME "Ole32" PREFIX "")
target_compile_definitions(Ole32 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(Pdh SHARED pdh_shim.cpp)
set_target_properties(Pdh PROPERTIES OUTPUT_NAME "Pdh" PREFIX "")
target_compile_definitions(Pdh PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
