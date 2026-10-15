cmake_minimum_required(VERSION 3.28)
project(geow2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
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
  src/game/model_geometry.cpp
)
target_link_libraries(geow2_game PUBLIC geow2_data)

add_executable(geow2-extract src/extract/main.cpp)
target_link_libraries(geow2-extract PRIVATE geow2_extract geow2_data geow2_game)

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
