cmake_minimum_required(VERSION 3.16)
project(gomoku_app CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

if(NOT MSVC)
  add_compile_options(-O3 -Wall -Wextra -fPIC)
endif()

# ---------------------------------------------------------------------------
# Qt6 and Dependencies
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Sql Widgets QuickControls2)
find_package(SQLite3 REQUIRED)
find_package(GTest QUIET)

# ---------------------------------------------------------------------------
# Include directories & Plugin SDK
# ---------------------------------------------------------------------------
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/include
)

if(NOT TARGET gomoku_plugin_sdk)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../plugin ${CMAKE_CURRENT_BINARY_DIR}/plugin_build EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# Application Core Library
# ---------------------------------------------------------------------------
set(APP_SOURCES
  src/core/database_manager.cpp
  src/core/board_view_model.cpp
  src/core/plugin_registry.cpp
  src/core/replay_controller.cpp
  src/core/game_controller.cpp
  src/models/match_history_model.cpp
  src/models/move_history_model.cpp
  qml/qml.qrc
)

add_library(app_core_lib STATIC ${APP_SOURCES})

target_link_libraries(app_core_lib PUBLIC
  gomoku_plugin_sdk
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::Qml
  Qt6::Sql
  Qt6::Widgets
  Qt6::QuickControls2
  SQLite::SQLite3
  ${CMAKE_DL_LIBS}
)

# ---------------------------------------------------------------------------
# Main Executable
# ---------------------------------------------------------------------------
add_executable(gomoku_app
  src/main.cpp
  qml/qml.qrc
)

target_link_libraries(gomoku_app PRIVATE
  app_core_lib
)

# ---------------------------------------------------------------------------
# Sample Engine Plugin with 'engine_plugin_' prefix for auto-discovery
# ---------------------------------------------------------------------------
add_library(engine_plugin_sample SHARED
  ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/sample_plugin/sample_engine_plugin.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/src/gomoku_types.cpp
)

target_include_directories(engine_plugin_sample PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/include
)

set_target_properties(engine_plugin_sample PROPERTIES
  OUTPUT_NAME "engine_plugin_sample"
  PREFIX ""
)

add_dependencies(gomoku_app engine_plugin_sample)

# Copy the sample engine plugin into the build directory alongside gomoku_app
add_custom_command(TARGET gomoku_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:engine_plugin_sample> $<TARGET_FILE_DIR:gomoku_app>
)

# ---------------------------------------------------------------------------
# Automated Unit Tests
# ---------------------------------------------------------------------------
if(GTest_FOUND OR TARGET GTest::gtest_main)
  enable_testing()

  add_executable(gomoku_app_test
    tests/board_view_model_test.cpp
    tests/database_manager_test.cpp
    tests/game_controller_test.cpp
    tests/plugin_registry_test.cpp
  )

  target_link_libraries(gomoku_app_test PRIVATE
    app_core_lib
    GTest::gtest_main
  )

  add_dependencies(gomoku_app_test engine_plugin_sample)

  add_custom_command(TARGET gomoku_app_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:engine_plugin_sample> $<TARGET_FILE_DIR:gomoku_app_test>
  )

  add_test(NAME gomoku_app_test COMMAND gomoku_app_test)
endif()
