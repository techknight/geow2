cmake_minimum_required(VERSION 3.28)
project(geow2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # Float results must match the original's instruction by instruction:
  # no reassociation and no implicit contraction into FMAs (/fp:precise
  # does neither since Visual Studio 2022; fused operations are written
  # explicitly with std::fma where the original uses them).
  add_compile_options(/W4 /permissive- /utf-8 /fp:precise)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Extractor: reads the user's XBLA package and writes the cache the game uses.
add_library(geow2_extract STATIC
  src/extract/aes128.cpp
  src/extract/io.cpp
  src/extract/sha1.cpp
  src/extract/lzx.cpp
  src/extract/stfs.cpp
  src/extract/xex.cpp
)
target_include_directories(geow2_extract PUBLIC src)

# Readers for the original data formats, shared by the extractor and the game.
add_library(geow2_data STATIC
  src/data/flask.cpp
  src/data/xtlo.cpp
)
target_link_libraries(geow2_data PUBLIC geow2_extract)

# Game logic and content built from the original data (no rendering, no platform code).
add_library(geow2_game STATIC
  src/game/bloom_weights.cpp
  src/game/bullets.cpp
  src/game/camera.cpp
  src/game/display_gamma.cpp
  src/game/game_math.cpp
  src/game/grid.cpp
  src/game/light_map.cpp
  src/game/line_quads.cpp
  src/game/model_geometry.cpp
  src/game/player_motion.cpp
  src/game/rotation.cpp
  src/game/stars.cpp
)
target_link_libraries(geow2_game PUBLIC geow2_data)

add_executable(geow2-extract src/extract/main.cpp)
target_link_libraries(geow2-extract PRIVATE geow2_extract geow2_data geow2_game)

# The game: SDL3 (window, input, GPU) and SPIR-V shaders compiled with glslc.
option(GEOW2_BUILD_GAME "Build the game (needs network access for SDL3 and the Vulkan SDK's glslc)" ON)
if(GEOW2_BUILD_GAME)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)

  file(GLOB VULKAN_SDK_BINS "C:/VulkanSDK/*/Bin")
  find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" ${VULKAN_SDK_BINS} REQUIRED)
  set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  set(SPIRV)
  foreach(shader line.vert line_body.frag line_cap.frag fullscreen.vert downsample.frag blur.frag combine.frag
          composite.frag grid.vert grid.frag light_warp.vert light_warp.frag light_sprite.vert light_sprite.frag
          light_combine.frag light_feedback.frag star.vert star.frag
          wall.vert wall.frag)
    set(out ${SHADER_OUT}/${shader}.spv)
    add_custom_command(OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
      COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shaders/${shader} -o ${out}
      DEPENDS src/app/shaders/${shader} src/app/shaders/sampling.glsl
      COMMENT "glslc ${shader}")
    list(APPEND SPIRV ${out})
  endforeach()
  add_custom_target(geow2_shaders DEPENDS ${SPIRV})

  add_executable(geow2 src/app/main.cpp src/app/png.cpp src/app/renderer.cpp)
  target_link_libraries(geow2 PRIVATE geow2_game SDL3::SDL3-static)
  add_dependencies(geow2 geow2_shaders)
endif()

enable_testing()
add_executable(extract_tests tests/extract_tests.cpp)
target_link_libraries(extract_tests PRIVATE geow2_extract)
add_test(NAME extract_tests COMMAND extract_tests)
add_executable(data_tests tests/data_tests.cpp)
target_link_libraries(data_tests PRIVATE geow2_data)
add_test(NAME data_tests COMMAND data_tests)
add_executable(game_tests tests/game_tests.cpp)
target_link_libraries(game_tests PRIVATE geow2_game)
add_test(NAME game_tests COMMAND game_tests)
