cmake_minimum_required(VERSION 3.20)
project(cybertunnel C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Pinned dependencies
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE)
FetchContent_Declare(raygui
  GIT_REPOSITORY https://github.com/raysan5/raygui.git
  GIT_TAG 4.0
  GIT_SHALLOW TRUE)
# raygui has no top-level CMakeLists, so this only downloads the header.
FetchContent_MakeAvailable(raylib raygui)

set(CT_WARN -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

set(CT_CORE_SRC
  src/core/noise.c
  src/core/path.c
  src/core/chunks.c
  src/core/flight.c
  src/core/params.c
  src/core/pacing.c
  src/render/linegeo.c
)
set(CT_MODULE_SRC
  ${CT_CORE_SRC}
  src/render/pipeline.c
  src/render/shaderlib.c
  src/env/env_vortex.c
  src/env/env_template.c
  src/env/env_city.c
  src/env/env_corridor.c
  src/env/corridor_pillars.c
  src/env/corridor_beams.c
  src/env/city_towers.c
  src/env/lattice_place.c
  src/env/city_decor.c
  src/env/trace_route.c
  src/render/streaks.c
  src/render/shapes.c
  src/render/gridfloor.c
  src/debug/debug_ui.c
  src/cybertunnel.c
)

# The module as a static library so the game can link it later.
add_library(cybertunnel_lib STATIC ${CT_MODULE_SRC})
target_include_directories(cybertunnel_lib PUBLIC src ${raygui_SOURCE_DIR}/src)
target_compile_options(cybertunnel_lib PRIVATE ${CT_WARN})
target_compile_definitions(cybertunnel_lib PUBLIC
  CT_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders"
  CT_PRESET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")
target_link_libraries(cybertunnel_lib PUBLIC raylib m)

add_executable(cybertunnel src/main.c)
target_compile_options(cybertunnel PRIVATE ${CT_WARN})
target_link_libraries(cybertunnel PRIVATE cybertunnel_lib)

add_executable(cybertunnel_tests tests/test_main.c)
target_compile_options(cybertunnel_tests PRIVATE ${CT_WARN})
target_link_libraries(cybertunnel_tests PRIVATE cybertunnel_lib)
