cmake_minimum_required(VERSION 3.21)
project(graphics_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(graphics_bridge SHARED
    presentation.cpp
    graphics_bridge.h
)

if(MSVC)
    set_property(TARGET graphics_bridge PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_compile_definitions(graphics_bridge PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(graphics_bridge PRIVATE d3d12 dxgi)
