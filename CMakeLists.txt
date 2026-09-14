cmake_minimum_required(VERSION 3.24)

project(TutonesMenuV2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Tutones-Menu-V2 SHARED
    src/dllmain.cpp
    src/app/Application.cpp
    src/backend/BackendHub.cpp
    src/core/Logger.cpp
    src/game/GameRuntime.cpp
    src/hooking/HookManager.cpp
    src/render/Renderer.cpp
    src/ui/Menu.cpp
)

target_include_directories(Tutones-Menu-V2 PRIVATE src)

target_compile_definitions(Tutones-Menu-V2 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(Tutones-Menu-V2 PRIVATE /W4 /permissive- /EHsc)
endif()

set_target_properties(Tutones-Menu-V2 PROPERTIES
    OUTPUT_NAME "Tutones-Menu-V2"
)
