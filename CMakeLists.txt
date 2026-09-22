cmake_minimum_required(VERSION 3.24)

project(TutonesMenuV2 VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(minhook)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(tutones_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)

target_include_directories(tutones_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(tutones_imgui PRIVATE d3d12 dxgi)

add_library(Tutones-Menu-V2 SHARED
    src/dllmain.cpp
    src/app/Application.cpp
    src/backend/BackendHub.cpp
    src/core/Logger.cpp
    src/config/SettingsService.cpp
    src/config/FeatureRestore.cpp
    src/game/GameProcess.cpp
    src/game/GameRuntime.cpp
    src/game/memory/ModuleView.cpp
    src/game/memory/PatternScanner.cpp
    src/game/memory/BytePatch.cpp
    src/game/native/NativePointers.cpp
    src/game/native/NativeRegistry.cpp
    src/game/script/ScriptRuntime.cpp
    src/features/player/PlayerService.cpp
    src/features/player/SelfOnlineService.cpp
    src/features/player/PlayerStatsService.cpp
    src/features/online/OnlineStatusService.cpp
    src/features/parity/ParityGlobalsService.cpp
    src/features/online/OnlinePlayerService.cpp
    src/features/utility/UtilityService.cpp
    src/features/weapon/WeaponAimPatches.cpp
    src/features/weapon/WeaponService.cpp
    src/features/vehicle/VehicleService.cpp
    src/features/vehicle/VehicleConvenienceService.cpp
    src/features/world/TeleportService.cpp
    src/features/world/WorldService.cpp
    src/hooking/HookManager.cpp
    src/render/Renderer.cpp
    src/ui/Menu.cpp
    src/ui/MenuTheme.cpp
    src/ui/PageRenderer.cpp
    src/ui/UtilityOverlay.cpp
)

target_include_directories(Tutones-Menu-V2 PRIVATE src)

target_compile_definitions(Tutones-Menu-V2 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(Tutones-Menu-V2 PRIVATE /W4 /permissive- /EHsc)
endif()

target_link_libraries(Tutones-Menu-V2 PRIVATE
    minhook
    tutones_imgui
    d3d12
    dxgi
    user32
    windowscodecs
    ole32
    winhttp
)

set_target_properties(Tutones-Menu-V2 PROPERTIES
    OUTPUT_NAME "Tutones-Menu-V2"
)
