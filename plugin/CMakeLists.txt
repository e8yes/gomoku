cmake_minimum_required(VERSION 3.14)
project(gomoku_plugin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

if(NOT MSVC)
  add_compile_options(-O3 -Wall -Wextra -fPIC)
endif()

# ---------------------------------------------------------------------------
# Plugin SDK Library
# ---------------------------------------------------------------------------
add_library(gomoku_plugin_sdk STATIC
  src/gomoku_types.cpp
  src/difficulty.cpp
  src/human_player.cpp
  src/plugin_loader.cpp
  src/match_coordinator.cpp
)

target_include_directories(gomoku_plugin_sdk PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(gomoku_plugin_sdk PUBLIC
  ${CMAKE_DL_LIBS}
)

# ---------------------------------------------------------------------------
# Sample Engine Plugin (Dynamic Shared Library)
# ---------------------------------------------------------------------------
add_library(gomoku_sample_engine SHARED
  sample_plugin/sample_engine_plugin.cpp
  src/gomoku_types.cpp
)

target_include_directories(gomoku_sample_engine PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(gomoku_sample_engine PROPERTIES
  OUTPUT_NAME "gomoku_sample_engine"
  PREFIX "lib"
)

# ---------------------------------------------------------------------------
# Unit Tests
# ---------------------------------------------------------------------------
find_package(GTest QUIET)

if(GTest_FOUND OR TARGET GTest::gtest_main)
  add_executable(gomoku_plugin_test
    tests/gomoku_types_test.cpp
    tests/difficulty_test.cpp
    tests/human_player_test.cpp
    tests/plugin_loader_test.cpp
    tests/match_coordinator_test.cpp
  )

  target_link_libraries(gomoku_plugin_test PRIVATE
    gomoku_plugin_sdk
    GTest::gtest_main
    ${CMAKE_DL_LIBS}
  )

  add_dependencies(gomoku_plugin_test gomoku_sample_engine)

  # Copy the shared library to the same directory as the test executable
  add_custom_command(TARGET gomoku_plugin_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gomoku_sample_engine> $<TARGET_FILE_DIR:gomoku_plugin_test>
  )

  enable_testing()
  add_test(NAME gomoku_plugin_test COMMAND gomoku_plugin_test)
endif()
