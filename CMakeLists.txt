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
target_include_directories(cybertunnel_lib PUBLIC src ${raygui_SOURCE_DIR}/src)
target_compile_options(cybertunnel_lib PRIVATE ${CT_WARN})
target_compile_definitions(cybertunnel_lib PUBLIC
  CT_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders"
  CT_PRESET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")
target_link_libraries(cybertunnel_lib PUBLIC raylib)
if(NOT WIN32)
  target_link_libraries(cybertunnel_lib PUBLIC m)
endif()

add_executable(cybertunnel src/main.c src/core/resources.c src/game/game.c src/game/mission.c src/game/techno.c)
if(WIN32)
  set_target_properties(cybertunnel PROPERTIES WIN32_EXECUTABLE TRUE)
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
  target_sources(cybertunnel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/game_assets.rc")
  target_include_directories(cybertunnel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
target_compile_options(cybertunnel PRIVATE ${CT_WARN})
target_link_libraries(cybertunnel PRIVATE cybertunnel_lib)
if(WIN32 AND MINGW)
  target_link_options(cybertunnel PRIVATE -static -static-libgcc)
endif()

add_custom_command(TARGET cybertunnel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders" "$<TARGET_FILE_DIR:cybertunnel>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/presets" "$<TARGET_FILE_DIR:cybertunnel>/presets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:cybertunnel>/assets")

add_executable(cybertunnel_tests tests/test_main.c)
target_compile_options(cybertunnel_tests PRIVATE ${CT_WARN})
target_link_libraries(cybertunnel_tests PRIVATE cybertunnel_lib)
if(WIN32 AND MINGW)
  target_link_options(cybertunnel_tests PRIVATE -static -static-libgcc)
endif()

enable_testing()
add_test(NAME cybertunnel_core COMMAND cybertunnel_tests)
add_executable(mission_tests tests/test_mission.c src/game/mission.c)
target_include_directories(mission_tests PRIVATE src)
target_compile_options(mission_tests PRIVATE ${CT_WARN})
target_link_libraries(mission_tests PRIVATE cybertunnel_lib)
if(NOT WIN32)
  target_link_libraries(mission_tests PRIVATE m)
elseif(MINGW)
  target_link_options(mission_tests PRIVATE -static -static-libgcc)
endif()
add_test(NAME data_run_mission COMMAND mission_tests)
add_executable(music_tests tests/test_music.c src/game/techno.c)
target_include_directories(music_tests PRIVATE src)
target_compile_options(music_tests PRIVATE ${CT_WARN})
add_executable(ship_tests tests/test_ship.c)
target_compile_options(ship_tests PRIVATE ${CT_WARN})
target_link_libraries(ship_tests PRIVATE cybertunnel_lib)
if(NOT WIN32)
  target_link_libraries(music_tests PRIVATE m)
elseif(MINGW)
  target_link_options(music_tests PRIVATE -static -static-libgcc)
  target_link_options(ship_tests PRIVATE -static -static-libgcc)
endif()
add_test(NAME data_run_techno COMMAND music_tests)
add_test(NAME data_run_ship COMMAND ship_tests)
add_executable(vectoring_tests tests/test_vectoring.c)
target_compile_options(vectoring_tests PRIVATE ${CT_WARN})
target_link_libraries(vectoring_tests PRIVATE cybertunnel_lib)
if(WIN32 AND MINGW)
  target_link_options(vectoring_tests PRIVATE -static -static-libgcc)
endif()
add_test(NAME data_run_vectoring COMMAND vectoring_tests)
if(WIN32)
  add_test(NAME data_run_endless COMMAND cybertunnel --hidden --autoplay --missions 6 --seed 83)
  set_tests_properties(data_run_endless PROPERTIES TIMEOUT 60)
  add_test(NAME data_run_startup COMMAND cybertunnel --hidden --startup-check)
  set_tests_properties(data_run_startup PROPERTIES TIMEOUT 20)
  add_test(NAME data_run_live_menu COMMAND cybertunnel --hidden --menu-check --seed 83)
  set_tests_properties(data_run_live_menu PROPERTIES TIMEOUT 30)
  add_test(NAME data_run_ship_systems COMMAND cybertunnel --hidden --ship-check --seed 83)
  set_tests_properties(data_run_ship_systems PROPERTIES TIMEOUT 20)
  add_test(NAME data_run_standalone COMMAND ${CMAKE_COMMAND}
    "-DEXECUTABLE=$<TARGET_FILE:cybertunnel>" "-DCHECK_DIR=${CMAKE_CURRENT_BINARY_DIR}/standalone-check"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_standalone.cmake")
  set_tests_properties(data_run_standalone PROPERTIES TIMEOUT 20)
endif()

install(TARGETS cybertunnel RUNTIME DESTINATION . COMPONENT Runtime)
install(DIRECTORY src/render/shaders/ DESTINATION shaders COMPONENT Runtime)
install(DIRECTORY presets/ DESTINATION presets COMPONENT Runtime)
install(DIRECTORY assets/ DESTINATION assets COMPONENT Runtime)
