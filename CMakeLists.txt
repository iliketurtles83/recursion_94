cmake_minimum_required(VERSION 3.20)
project(recursion94 C)

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
  GIT_TAG 6.0
  GIT_SHALLOW TRUE)
FetchContent_Declare(raygui
  GIT_REPOSITORY https://github.com/raysan5/raygui.git
  GIT_TAG 4.0
  GIT_SHALLOW TRUE)
# raygui has no top-level CMakeLists, so this only downloads the header.
FetchContent_MakeAvailable(raylib raygui)

if(MSVC)
  set(CT_WARN /W4 /wd4100)
else()
  set(CT_WARN -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

set(CT_CORE_SRC
  src/core/noise.c
  src/core/path.c
  src/core/chunks.c
  src/core/flight.c
  src/core/params.c
  src/core/pacing.c
  src/render/linegeo.c
  src/env/city_landmarks.c
  src/render/shipmodel.c
  src/render/tunneldetail.c
)
set(CT_MODULE_SRC
  ${CT_CORE_SRC}
  src/render/pipeline.c
  src/render/shaderlib.c
  src/env/env_vortex.c
  src/env/env_template.c
  src/env/env_city.c
  src/env/env_corridor.c
  src/env/env_greybox.c
  src/env/env_crystal.c
  src/env/crystal_field.c
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
target_include_directories(cybertunnel_lib PUBLIC src)
target_include_directories(cybertunnel_lib SYSTEM PUBLIC ${raygui_SOURCE_DIR}/src)   # vendor header: keep its warnings out
target_compile_options(cybertunnel_lib PRIVATE ${CT_WARN})
target_compile_definitions(cybertunnel_lib PUBLIC
  CT_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders"
  CT_PRESET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")
target_link_libraries(cybertunnel_lib PUBLIC raylib)
if(NOT WIN32)
  target_link_libraries(cybertunnel_lib PUBLIC m)
endif()

add_executable(recursion94 src/main.c src/core/resources.c
  src/game/game.c src/game/audio_synth.c src/game/demoscene.c src/game/gameplay.c
  src/game/combat_draw.c src/game/songs.c src/game/story.c src/game/titles.c)
target_include_directories(recursion94 PRIVATE src/game)
if(WIN32)
  set_target_properties(recursion94 PROPERTIES WIN32_EXECUTABLE TRUE)
  enable_language(RC)
  file(GLOB CT_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/*")
  file(GLOB CT_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/presets/*")
  file(GLOB CT_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
  set(CT_RESOURCE_RC "")
  set(CT_RESOURCE_CATALOG "static const ResourceItem resourceItems[] = {\n")
  set(CT_RESOURCE_ID 1000)
  foreach(CT_RESOURCE_GROUP IN ITEMS SHADERS PRESETS ASSETS)
    string(TOLOWER "${CT_RESOURCE_GROUP}" CT_RESOURCE_FOLDER)
    foreach(CT_RESOURCE_FILE IN LISTS CT_${CT_RESOURCE_GROUP})
      if(NOT IS_DIRECTORY "${CT_RESOURCE_FILE}")
        math(EXPR CT_RESOURCE_ID "${CT_RESOURCE_ID} + 1")
        get_filename_component(CT_RESOURCE_NAME "${CT_RESOURCE_FILE}" NAME)
        string(APPEND CT_RESOURCE_RC "${CT_RESOURCE_ID} RCDATA \"${CT_RESOURCE_FILE}\"\n")
        string(APPEND CT_RESOURCE_CATALOG "  {\"${CT_RESOURCE_FOLDER}/${CT_RESOURCE_NAME}\", ${CT_RESOURCE_ID}},\n")
      endif()
    endforeach()
  endforeach()
  string(APPEND CT_RESOURCE_CATALOG "};\n")
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/game_assets.rc" CONTENT "${CT_RESOURCE_RC}")
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/resources_catalog.h" CONTENT "${CT_RESOURCE_CATALOG}")
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/game_assets.rc" PROPERTIES
    OBJECT_DEPENDS "${CT_SHADERS};${CT_PRESETS};${CT_ASSETS}")
  target_sources(recursion94 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/game_assets.rc")
  target_include_directories(recursion94 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
target_compile_options(recursion94 PRIVATE ${CT_WARN})
target_link_libraries(recursion94 PRIVATE cybertunnel_lib)
if(WIN32 AND MINGW)
  target_link_options(recursion94 PRIVATE -static -static-libgcc)
endif()

add_custom_command(TARGET recursion94 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders" "$<TARGET_FILE_DIR:recursion94>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/presets" "$<TARGET_FILE_DIR:recursion94>/presets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:recursion94>/assets")

add_executable(cybertunnel_tests tests/test_main.c)
target_compile_options(cybertunnel_tests PRIVATE ${CT_WARN})
target_link_libraries(cybertunnel_tests PRIVATE cybertunnel_lib)
if(WIN32 AND MINGW)
  target_link_options(cybertunnel_tests PRIVATE -static -static-libgcc)
endif()

enable_testing()
add_executable(audio_tests tests/test_audio.c src/game/audio_synth.c src/game/songs.c)
target_include_directories(audio_tests PRIVATE src/game)
target_compile_options(audio_tests PRIVATE ${CT_WARN})
target_link_libraries(audio_tests PRIVATE raylib)
if(NOT WIN32)
  target_link_libraries(audio_tests PRIVATE m)
elseif(MINGW)
  target_link_options(audio_tests PRIVATE -static -static-libgcc)
endif()
add_test(NAME game_audio COMMAND audio_tests)
# Offline song render (WAV + per-layer levels); a tool, not a test.
add_executable(song_render tests/song_render.c src/game/audio_synth.c src/game/songs.c)
target_include_directories(song_render PRIVATE src/game)
target_compile_definitions(song_render PRIVATE SYNTH_METER)
target_compile_options(song_render PRIVATE ${CT_WARN})
target_link_libraries(song_render PRIVATE raylib)
if(NOT WIN32)
  target_link_libraries(song_render PRIVATE m)
endif()
add_executable(gameplay_tests tests/test_gameplay.c src/game/gameplay.c src/game/story.c)
target_include_directories(gameplay_tests PRIVATE src/game)
target_compile_options(gameplay_tests PRIVATE ${CT_WARN})
target_link_libraries(gameplay_tests PRIVATE raylib)
if(NOT WIN32)
  target_link_libraries(gameplay_tests PRIVATE m)
elseif(MINGW)
  target_link_options(gameplay_tests PRIVATE -static -static-libgcc)
endif()
add_test(NAME game_combat COMMAND gameplay_tests)
add_test(NAME engine_core COMMAND cybertunnel_tests)
add_executable(ship_tests tests/test_ship.c)
target_compile_options(ship_tests PRIVATE ${CT_WARN})
target_link_libraries(ship_tests PRIVATE cybertunnel_lib)
if(WIN32 AND MINGW)
  target_link_options(ship_tests PRIVATE -static -static-libgcc)
endif()
add_test(NAME engine_ship COMMAND ship_tests)
add_executable(vectoring_tests tests/test_vectoring.c)
target_compile_options(vectoring_tests PRIVATE ${CT_WARN})
target_link_libraries(vectoring_tests PRIVATE cybertunnel_lib)
if(WIN32 AND MINGW)
  target_link_options(vectoring_tests PRIVATE -static -static-libgcc)
endif()
add_test(NAME engine_vectoring COMMAND vectoring_tests)
install(TARGETS recursion94 RUNTIME DESTINATION . COMPONENT Runtime)
install(DIRECTORY src/render/shaders/ DESTINATION shaders COMPONENT Runtime)
install(DIRECTORY presets/ DESTINATION presets COMPONENT Runtime)
install(DIRECTORY assets/ DESTINATION assets COMPONENT Runtime)
