cmake_minimum_required(VERSION 3.21)
project(xbox_openal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# xbox-openal.dll - drop-in replacement for OpenAL.dll on Xbox UWP.  Core AL /
# ALC functions are exported via __declspec(dllexport) in xbox_openal.cpp;
# EFX / SOFT extensions alias to universal_no_op through xbox_openal.def so
# GetProcAddress on uncommon names doesn't fail.  LWJGL is pointed at this
# DLL via -Dorg.lwjgl.openal.libname=<absolute path>.

add_library(xbox_openal SHARED
    xbox_openal.cpp
    xbox_openal.def
)

set_target_properties(xbox_openal PROPERTIES
    OUTPUT_NAME "xbox-openal"
    PREFIX ""
)

if(MSVC)
    set_property(TARGET xbox_openal PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(xbox_openal PRIVATE /W3 /permissive-)
endif()

target_compile_definitions(xbox_openal PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(xbox_openal PRIVATE xaudio2 ole32)
