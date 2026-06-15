cmake_minimum_required(VERSION 3.21)
project(xbox_opengl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# xbox-opengl.dll - drop-in replacement for opengl32.dll on Xbox UWP.  All 368
# exports the real opengl32 ships are listed in xbox_opengl.def; ~340 alias to
# universal_no_op, the rest map to bespoke implementations in xbox_opengl.cpp.
# LWJGL is pointed here via -Dorg.lwjgl.opengl.libname=<absolute path>.

add_library(xbox_opengl SHARED
    xbox_opengl.cpp
    xbox_opengl.def
)

set_target_properties(xbox_opengl PROPERTIES
    OUTPUT_NAME "xbox-opengl"
    PREFIX ""
)

if(MSVC)
    set_property(TARGET xbox_opengl PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(xbox_opengl PRIVATE /W3 /permissive-)
endif()

target_compile_definitions(xbox_opengl PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

target_link_libraries(xbox_opengl PRIVATE runtimeobject)
