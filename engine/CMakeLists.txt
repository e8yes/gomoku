cmake_minimum_required(VERSION 3.14)
project(gomoku_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto=auto")
set(CMAKE_LINKER_FLAGS_RELEASE "${CMAKE_LINKER_FLAGS_RELEASE} -O3 -flto=auto")

find_package(GTest REQUIRED)

enable_testing()

include_directories(src)

set(ENGINE_SOURCES
  src/board.cpp
  src/random_evaluator.cpp
  src/mcts.cpp
)

add_library(engine_lib ${ENGINE_SOURCES})

add_executable(engine_tests tests/board_test.cpp tests/mcts_test.cpp)
target_link_libraries(engine_tests engine_lib GTest::gtest_main)
add_test(NAME engine_tests COMMAND engine_tests)
