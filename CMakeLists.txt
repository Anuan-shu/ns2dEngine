cmake_minimum_required(VERSION 3.16)
project(ns2d_physics_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
add_executable(ecs_tests tests/ecs_tests.cpp src/ECS/ECS.cpp)
target_compile_features(ecs_tests PRIVATE cxx_std_17)
target_include_directories(ecs_tests SYSTEM PRIVATE libs)
add_test(NAME ecs_tests COMMAND ecs_tests)
add_executable(physics_tests tests/physics_tests.cpp)
target_compile_features(physics_tests PRIVATE cxx_std_17)
target_include_directories(physics_tests SYSTEM PRIVATE libs)
add_test(NAME physics_tests COMMAND physics_tests)

# 可选集成测试：使用真实 SDL 头文件、链接库和项目中的 ECS。
set(SDL2_INCLUDE_DIR "" CACHE PATH "Directory containing SDL.h")
set(SDL2_LIBRARY "" CACHE FILEPATH "SDL2 import library")
if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
    add_executable(physics_ecs_tests tests/physics_ecs_tests.cpp src/ECS/ECS.cpp src/Logger/Logger.cpp)
    target_compile_features(physics_ecs_tests PRIVATE cxx_std_17)
    target_include_directories(physics_ecs_tests SYSTEM PRIVATE libs "${SDL2_INCLUDE_DIR}")
    target_link_libraries(physics_ecs_tests PRIVATE "${SDL2_LIBRARY}")
    add_test(NAME physics_ecs_tests COMMAND physics_ecs_tests)
    set_tests_properties(physics_ecs_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

option(NS2D_BUILD_GAME "Build the game and headless smoke tests (Windows/MinGW)" OFF)
set(SDL2_IMAGE_ROOT "" CACHE PATH "SDL_image x86_64-w64-mingw32 directory")
set(SDL2_TTF_ROOT "" CACHE PATH "SDL_ttf x86_64-w64-mingw32 directory")
if(NS2D_BUILD_GAME)
    if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY OR NOT SDL2_IMAGE_ROOT OR NOT SDL2_TTF_ROOT)
        message(FATAL_ERROR "Set SDL2_INCLUDE_DIR, SDL2_LIBRARY, SDL2_IMAGE_ROOT, SDL2_TTF_ROOT")
    endif()
    add_library(ns2d_game_core STATIC
        src/Game/Game.cpp src/Game/LevelLoader.cpp src/ECS/ECS.cpp
        src/Logger/Logger.cpp src/AssetStore/AssetStore.cpp
        src/Editor/SceneEditor.cpp src/Editor/SceneSerializer.cpp src/Systems/RenderGUISystem.cpp
        libs/imgui/imgui.cpp libs/imgui/imgui_draw.cpp libs/imgui/imgui_widgets.cpp
        libs/imgui/imgui_sdl.cpp libs/imgui/imgui_impl_sdl.cpp)
    target_compile_features(ns2d_game_core PUBLIC cxx_std_17)
    target_compile_definitions(ns2d_game_core PUBLIC SDL_MAIN_HANDLED)
    target_include_directories(ns2d_game_core SYSTEM PUBLIC libs libs/lua "${SDL2_INCLUDE_DIR}"
        "${SDL2_IMAGE_ROOT}/include/SDL2" "${SDL2_TTF_ROOT}/include/SDL2")
    # 项目附带的旧版 sol 存在未使用的模板，GCC 16 的提前检查会将其判为错误。
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
        target_compile_options(ns2d_game_core PUBLIC -Wno-template-body)
    endif()
    target_link_libraries(ns2d_game_core PUBLIC "${SDL2_LIBRARY}"
        "${SDL2_IMAGE_ROOT}/lib/libSDL2_image.dll.a" "${SDL2_TTF_ROOT}/lib/libSDL2_ttf.dll.a"
        "${CMAKE_SOURCE_DIR}/libs/lua/liblua53.a")
    add_executable(ns2dEngine src/Main.cpp)
    target_link_libraries(ns2dEngine PRIVATE ns2d_game_core)
    add_executable(physics_game_smoke tests/physics_game_smoke.cpp)
    target_link_libraries(physics_game_smoke PRIVATE ns2d_game_core)
    add_executable(scene_editor_tests tests/scene_editor_tests.cpp)
    target_link_libraries(scene_editor_tests PRIVATE ns2d_game_core)
    add_test(NAME scene_editor_tests COMMAND scene_editor_tests)
    set_tests_properties(scene_editor_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 30)
    add_executable(scene_editor_game_tests tests/scene_editor_game_tests.cpp)
    target_link_libraries(scene_editor_game_tests PRIVATE ns2d_game_core)
    add_executable(scene_editor_ui_tests tests/scene_editor_ui_tests.cpp)
    target_link_libraries(scene_editor_ui_tests PRIVATE ns2d_game_core)
    add_test(NAME scene_editor_ui_tests COMMAND scene_editor_ui_tests)
    set_tests_properties(scene_editor_ui_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
    foreach(level 2 3)
        add_test(NAME game_level_${level}_smoke COMMAND physics_game_smoke ${level})
        set_tests_properties(game_level_${level}_smoke PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
    endforeach()
    foreach(level 2 3)
        add_test(NAME editor_level_${level}_integration COMMAND scene_editor_game_tests ${level})
        set_tests_properties(editor_level_${level}_integration PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
    endforeach()
endif()
