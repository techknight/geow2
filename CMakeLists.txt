cmake_minimum_required(VERSION 3.28)
# macOS 11 is the oldest the game supports (before project(), which reads it).
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Oldest macOS to run on")
project(geow2 VERSION 1.0.0 LANGUAGES CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The C runtime linked in, so a packaged build needs no redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  # Float results must match the original's instruction by instruction:
  # no reassociation and no implicit contraction into FMAs (/fp:precise
  # does neither since Visual Studio 2022; fused operations are written
  # explicitly with std::fma where the original uses them).
  add_compile_options(/W4 /permissive- /utf-8 /fp:precise)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  # The same rules for GCC and Clang: no implicit contraction (GCC's
  # default is -ffp-contract=fast and Apple Clang's =on, which fuse
  # a * b + c on any FMA-capable target, arm64 included).
  add_compile_options(-ffp-contract=off)
  # x87 arithmetic keeps excess precision; the replays need SSE or NEON.
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "32-bit builds are not supported: their float arithmetic does not match the original's")
  endif()
endif()

# Installed as one playable folder (the release archives), or with
# GEOW2_UNIX_LAYOUT as a Linux package expects (bin/, share/; the Flatpak).
option(GEOW2_UNIX_LAYOUT "Install in a bin/ and share/ layout (Linux packages)" OFF)
set(GEOW2_APP_ID com.geow2.geow2)

# Extractor: reads the user's XBLA package and writes the data folder the game uses.
add_library(geow2_extract STATIC
  src/extract/aes128.cpp
  src/extract/audio.cpp
  src/extract/io.cpp
  src/extract/sha1.cpp
  src/extract/lzx.cpp
  src/extract/stfs.cpp
  src/extract/unpack.cpp
  src/extract/xex.cpp
)
target_include_directories(geow2_extract PUBLIC src)
# version.h from project(VERSION).
configure_file(src/app/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)
target_include_directories(geow2_extract PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Windows: the icon and version resources and the manifest (UTF-8 code
# page, long paths) for one executable.
function(geow2_windows_resources target description)
  if(NOT WIN32)
    return()
  endif()
  set(RC_NAME ${target})
  set(RC_DESCRIPTION ${description})
  set(RC_ICON ${CMAKE_CURRENT_SOURCE_DIR}/res/geow2.ico)
  configure_file(res/geow2.rc.in ${CMAKE_CURRENT_BINARY_DIR}/generated/${target}.rc @ONLY)
  target_sources(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/${target}.rc res/geow2.manifest)
endfunction()

# Readers for the original data formats, shared by the extractor and the game.
add_library(geow2_data STATIC
  src/data/flask.cpp
  src/data/png_read.cpp
  src/data/tga.cpp
  src/data/xdbf.cpp
  src/data/xtlo.cpp
)
target_link_libraries(geow2_data PUBLIC geow2_extract)

# Game logic and content built from the original data (no rendering, no platform code).
add_library(geow2_game STATIC
  src/game/amoeba.cpp
  src/game/bloom_weights.cpp
  src/game/bullets.cpp
  src/game/camera.cpp
  src/game/deadline.cpp
  src/game/debris.cpp
  src/game/display_gamma.cpp
  src/game/effects.cpp
  src/game/enemies.cpp
  src/game/font.cpp
  src/game/game_math.cpp
  src/game/gate.cpp
  src/game/geoms.cpp
  src/game/grid.cpp
  src/game/hill.cpp
  src/game/hud.cpp
  src/game/gravity_well.cpp
  src/game/killer_ghost.cpp
  src/game/light_map.cpp
  src/game/line_quads.cpp
  src/game/model_geometry.cpp
  src/game/particles.cpp
  src/game/player.cpp
  src/game/popups.cpp
  src/game/player_motion.cpp
  src/game/profile.cpp
  src/game/repulsar.cpp
  src/game/rotation.cpp
  src/game/score_log.cpp
  src/game/sequence.cpp
  src/game/shockwave.cpp
  src/game/shots.cpp
  src/game/snake.cpp
  src/game/spawn.cpp
  src/game/wave_spawner.cpp
  src/game/power_up.cpp
  src/game/stars.cpp
  src/game/ui.cpp
  src/game/ui_text.cpp
  src/game/units.cpp
  src/game/wanderer.cpp
  src/game/world.cpp
)
target_link_libraries(geow2_game PUBLIC geow2_data)

# Online scores: JSON, HTTPS through the system's library (WinHTTP, or
# libcurl loaded at run time) and the score server's API.
add_library(geow2_net STATIC
  src/net/http.cpp
  src/net/json.cpp
  src/net/scores.cpp
  src/third_party/qrcodegen/qrcodegen.cpp
)
target_link_libraries(geow2_net PUBLIC geow2_game)
if(WIN32)
  target_link_libraries(geow2_net PRIVATE winhttp)
else()
  target_link_libraries(geow2_net PRIVATE ${CMAKE_DL_LIBS})
endif()

add_executable(geow2-extract src/extract/main.cpp)
# (geow2_game brings geow2_data and geow2_extract; naming them again duplicates them on the link line.)
target_link_libraries(geow2-extract PRIVATE geow2_game)
geow2_windows_resources(geow2-extract "geow2 data extractor")

# The game: SDL3 (window, input, GPU) and SPIR-V shaders compiled with glslc.
option(GEOW2_BUILD_GAME "Build the game (needs network access for SDL3 and the Vulkan SDK's glslc)" ON)
if(GEOW2_BUILD_GAME)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  # SDL's 3.4.18 release archive, checked against its SHA-256.
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
  FetchContent_MakeAvailable(SDL3)

  file(GLOB VULKAN_SDK_BINS "C:/VulkanSDK/*/Bin")
  find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" ${VULKAN_SDK_BINS} REQUIRED)
  # Metal (macOS) takes MSL: each SPIR-V file translated by spirv-cross with
  # the resources at their SPIR-V binding numbers, which is SDL's MSL layout.
  if(APPLE)
    find_program(SPIRV_CROSS spirv-cross HINTS "$ENV{VULKAN_SDK}/bin" REQUIRED)
  endif()
  set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  set(SPIRV)
  foreach(shader line.vert ui_line.vert ui_fill.frag ui_sprite.vert ui_sprite_flat.vert ui_sprite.frag line_body.frag line_cap.frag fullscreen.vert downsample.frag blur.frag combine.frag
          composite.frag grid.vert grid.frag light_warp.vert light_warp.frag light_sprite.vert light_sprite.frag
          light_combine.frag light_feedback.frag star.vert star.frag
          wall.vert wall.frag particle.frag)
    set(out ${SHADER_OUT}/${shader}.spv)
    add_custom_command(OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
      COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shaders/${shader} -o ${out}
      DEPENDS src/app/shaders/${shader} src/app/shaders/sampling.glsl
      COMMENT "glslc ${shader}")
    list(APPEND SPIRV ${out})
    if(APPLE)
      set(msl ${SHADER_OUT}/${shader}.msl)
      add_custom_command(OUTPUT ${msl}
        COMMAND ${SPIRV_CROSS} ${out} --msl --msl-version 20100 --msl-decoration-binding --output ${msl}
        DEPENDS ${out}
        COMMENT "spirv-cross ${shader}")
      list(APPEND SPIRV ${msl})
    endif()
  endforeach()
  add_custom_target(geow2_shaders DEPENDS ${SPIRV})

  # A window program on Windows (no console), an app bundle on macOS.
  add_executable(geow2 WIN32 MACOSX_BUNDLE
    src/app/console.cpp src/app/frontend.cpp src/app/game.cpp src/app/main.cpp src/app/png.cpp src/app/renderer.cpp
    src/app/online.cpp src/app/setup.cpp)
  target_link_libraries(geow2 PRIVATE geow2_game geow2_audio geow2_net SDL3::SDL3-static)
  add_dependencies(geow2 geow2_shaders)
  geow2_windows_resources(geow2 "geow2")
  if(APPLE)
    # geow2.app: the icon and the shaders in Contents/Resources, where
    # SDL_GetBasePath() points.
    configure_file(res/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Info.plist @ONLY)
    set_target_properties(geow2 PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/generated/Info.plist)
    target_sources(geow2 PRIVATE res/geow2.icns ${SPIRV})
    set_source_files_properties(res/geow2.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties(${SPIRV} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/shaders GENERATED TRUE)
  endif()

  if(GEOW2_UNIX_LAYOUT)
    # bin/, share/geow2/ (shaders and the window's icon), the desktop entry,
    # metainfo and icons under the app id, and the licences.
    install(TARGETS geow2 RUNTIME DESTINATION bin)
    install(FILES ${SPIRV} DESTINATION share/geow2/shaders)
    install(FILES res/linux/geow2-256.png DESTINATION share/geow2 RENAME geow2.png)
    set(DESKTOP_ICON ${GEOW2_APP_ID})
    configure_file(res/geow2.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/generated/${GEOW2_APP_ID}.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/${GEOW2_APP_ID}.desktop DESTINATION share/applications)
    install(FILES res/${GEOW2_APP_ID}.metainfo.xml DESTINATION share/metainfo)
    foreach(size 16 32 48 64 128 256 512)
      install(FILES res/linux/geow2-${size}.png DESTINATION share/icons/hicolor/${size}x${size}/apps
              RENAME ${GEOW2_APP_ID}.png)
    endforeach()
  else()
    # A playable folder (cmake --install build --prefix <dir>): the game,
    # the extractor, the shaders and the documents. The user adds the data
    # folder.
    install(TARGETS geow2 RUNTIME DESTINATION . BUNDLE DESTINATION .)
    if(NOT APPLE)
      install(FILES ${SPIRV} DESTINATION shaders)
    endif()
    if(UNIX AND NOT APPLE)
      # The window's icon (read at start) and a desktop entry.
      install(FILES res/linux/geow2-256.png DESTINATION . RENAME geow2.png)
      set(DESKTOP_ICON geow2)
      configure_file(res/geow2.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/generated/geow2.desktop @ONLY)
      install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/geow2.desktop DESTINATION .)
    endif()
  endif()
endif()
if(GEOW2_UNIX_LAYOUT)
  install(TARGETS geow2-extract RUNTIME DESTINATION bin)
  install(FILES LICENSE THIRD-PARTY-NOTICES.md DESTINATION share/licenses/${GEOW2_APP_ID})
  install(FILES README.md CHANGELOG.md DIFFERENCES.md DESTINATION share/doc/geow2)
else()
  install(TARGETS geow2-extract RUNTIME DESTINATION .)
  install(FILES README.md CHANGELOG.md LICENSE DIFFERENCES.md THIRD-PARTY-NOTICES.md DESTINATION .)
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
add_executable(net_tests tests/net_tests.cpp)
target_link_libraries(net_tests PRIVATE geow2_net)
add_test(NAME net_tests COMMAND net_tests)

# Audio engine: the AOBJ component graphs (sounds, mixers, filters,
# scripts), the waves and the 5.1 mix; the SDL3 output when the game is
# built.
add_library(geow2_audio STATIC
  src/audio/aobj.cpp
  src/audio/components_control.cpp
  src/audio/components_mix.cpp
  src/audio/components_source.cpp
  src/audio/components_effect.cpp
  src/audio/engine.cpp
  src/audio/game_audio.cpp
  src/audio/graph.cpp
  src/audio/reverb.cpp
  src/audio/waves.cpp
)
target_link_libraries(geow2_audio PUBLIC geow2_extract)
if(TARGET SDL3::SDL3-static)
  target_sources(geow2_audio PRIVATE src/audio/device.cpp)
  target_link_libraries(geow2_audio PUBLIC SDL3::SDL3-static)
endif()
add_executable(audio_tests tests/audio_tests.cpp)
target_link_libraries(audio_tests PRIVATE geow2_audio)
add_test(NAME audio_tests COMMAND audio_tests)

# On Linux, like the static C runtime on Windows: the C++ runtime linked
# in, so the programs need only the system's C library.
if(UNIX AND NOT APPLE)
  foreach(t geow2 geow2-extract)
    if(TARGET ${t})
      target_link_options(${t} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
  endforeach()
endif()

# Warnings for our own targets (not SDL's).
if(NOT MSVC)
  foreach(t geow2_extract geow2_data geow2_game geow2_audio geow2_net geow2-extract geow2
            extract_tests data_tests game_tests audio_tests net_tests)
    if(TARGET ${t})
      target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
  endforeach()
endif()
