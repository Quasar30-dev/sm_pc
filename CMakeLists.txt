# sm_pc: a native PC engine for Ratchet & Clank: Size Matters (PS2, USA) that reads the original disc image.
# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.24)
project(sm_pc VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "sm_pc builds for Windows (Direct3D 11, XInput, Media Foundation).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The released executable needs no Visual C++ runtime installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  add_compile_options(/W3 /EHsc /utf-8 /MP)
endif()
# SM_PC_STANDALONE: this repository's build, without the play options borrowed from the other PS2 Ratchet & Clank
# games (src/sm/extras.h). Their engine is not part of this repository.
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE SM_PC_STANDALONE)

include(FetchContent)

# ---- Dear ImGui (docking), MIT ----
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.5-docking
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  src/platform/imgui_win32_safe.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ---- meshoptimizer's simplifier (a subset, unmodified), MIT ----
add_library(meshoptimizer_simplify STATIC
  third_party/meshoptimizer/allocator.cpp
  third_party/meshoptimizer/indexgenerator.cpp
  third_party/meshoptimizer/simplifier.cpp)
target_include_directories(meshoptimizer_simplify PUBLIC third_party/meshoptimizer)

# ---- hig: readers for the disc's own formats (WADs, levels, actors, animation, HUD, audio, text) ----
file(GLOB HIG_SOURCES CONFIGURE_DEPENDS src/hig/*.cpp)
add_library(hig_core STATIC ${HIG_SOURCES})
target_include_directories(hig_core PUBLIC src)
target_link_libraries(hig_core PRIVATE meshoptimizer_simplify)

# ---- capture: screenshots and video recording (Media Foundation) ----
file(GLOB MEDIA_SOURCES CONFIGURE_DEPENDS src/media/*.cpp)
add_library(media_capture STATIC ${MEDIA_SOURCES})
target_include_directories(media_capture PUBLIC src)
target_link_libraries(media_capture PUBLIC imgui d3d11 d3dcompiler mfplat mfreadwrite mfuuid windowscodecs ole32 shell32)

# ---- sm_pc: the game ----
file(GLOB SM_SOURCES CONFIGURE_DEPENDS src/sm/*.cpp)
add_executable(sm_pc ${SM_SOURCES})
# compat first: the stand-in headers for the engine this repository does not carry (compat/ps2game)
target_include_directories(sm_pc PRIVATE compat src)
target_link_libraries(sm_pc PRIVATE hig_core media_capture imgui d3d11 dxgi d3dcompiler xinput9_1_0 winmm xaudio2 ole32)
if(MSVC)
  target_link_options(sm_pc PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()
