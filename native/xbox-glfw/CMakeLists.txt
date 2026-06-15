cmake_minimum_required(VERSION 3.21)
project(xbox_glfw LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# We build a DLL whose on-disk name is xbox-glfw.dll so it doesn't collide
# with LWJGL's own glfw.dll if both are ever present.  At runtime the launcher
# either points LWJGL at this file via -Dorg.lwjgl.glfw.libname=<absolute path>
# or overwrites the extracted glfw.dll with this one - see RuntimeAssetStager.
add_library(xbox_glfw SHARED
    xbox_glfw.cpp
)

set_target_properties(xbox_glfw PROPERTIES
    OUTPUT_NAME "xbox-glfw"
    PREFIX ""
)

if(MSVC)
    set_property(TARGET xbox_glfw PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(xbox_glfw PRIVATE /W3 /permissive-)
endif()

target_compile_definitions(xbox_glfw PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

target_link_libraries(xbox_glfw PRIVATE runtimeobject)
