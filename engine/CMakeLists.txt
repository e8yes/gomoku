cmake_minimum_required(VERSION 3.14)
project(gomoku_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE
      "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto=auto")
  set(CMAKE_LINKER_FLAGS_RELEASE
      "${CMAKE_LINKER_FLAGS_RELEASE} -O3 -flto=auto")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(GTest REQUIRED)
find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

enable_testing()

include_directories(src)

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------
set(ENGINE_SOURCES
  src/board.cpp
  src/random_evaluator.cpp
  src/mcts.cpp
  src/batch_inference_executor.cpp
  src/neural_net_evaluator.cpp
  src/vcf_solver.cpp
  src/game_data.cpp
  src/self_play.cpp
)

add_library(engine_lib ${ENGINE_SOURCES})
target_link_libraries(engine_lib "${TORCH_LIBRARIES}")

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------

add_executable(gomoku_engine_test
  tests/board_test.cpp
  tests/mcts_test.cpp
  tests/retract_test.cpp
  tests/batched_blocking_queue_test.cpp
  tests/neural_net_evaluator_test.cpp
  tests/vcf_solver_test.cpp
  tests/mcts_endgame_solver_test.cpp
  tests/mcts_noise_test.cpp
  tests/game_data_test.cpp
  tests/self_play_test.cpp
)
target_link_libraries(gomoku_engine_test engine_lib GTest::gtest_main "${TORCH_LIBRARIES}")
add_test(NAME gomoku_engine_test COMMAND gomoku_engine_test)

# ---------------------------------------------------------------------------
# Phase 5/6 self-play data generator executable
# ---------------------------------------------------------------------------

add_executable(gomoku_game_generator src/gomoku_game_generator.cpp)
target_link_libraries(gomoku_game_generator engine_lib "${TORCH_LIBRARIES}")
add_custom_command(TARGET gomoku_game_generator POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gomoku_game_generator> ${CMAKE_CURRENT_SOURCE_DIR}/../python/
)

# ---------------------------------------------------------------------------
# Phase 7 model evaluation executable
# ---------------------------------------------------------------------------

add_executable(gomoku_model_evaluator src/gomoku_model_evaluator.cpp)
target_link_libraries(gomoku_model_evaluator engine_lib "${TORCH_LIBRARIES}")
add_custom_command(TARGET gomoku_model_evaluator POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gomoku_model_evaluator> ${CMAKE_CURRENT_SOURCE_DIR}/../python/
)

