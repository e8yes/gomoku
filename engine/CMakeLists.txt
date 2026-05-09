cmake_minimum_required(VERSION 3.14)
project(gomoku_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto=auto")
set(CMAKE_LINKER_FLAGS_RELEASE "${CMAKE_LINKER_FLAGS_RELEASE} -O3 -flto=auto")

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
)

add_library(engine_lib ${ENGINE_SOURCES})
target_link_libraries(engine_lib "${TORCH_LIBRARIES}")

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------

# Phase 1: board logic and MCTS with RandomEvaluator
add_executable(engine_tests
  tests/board_test.cpp
  tests/mcts_test.cpp
  tests/retract_test.cpp
)
target_link_libraries(engine_tests engine_lib GTest::gtest_main "${TORCH_LIBRARIES}")
add_test(NAME engine_tests COMMAND engine_tests)

# Phase 2: BatchedBlockingQueue (no model required)
add_executable(batched_blocking_queue_tests
  tests/batched_blocking_queue_test.cpp
)
target_link_libraries(batched_blocking_queue_tests engine_lib GTest::gtest_main "${TORCH_LIBRARIES}")
add_test(NAME batched_blocking_queue_tests COMMAND batched_blocking_queue_tests)

# Phase 2: NeuralNetEvaluator encoding + end-to-end inference
add_executable(neural_net_evaluator_tests
  tests/neural_net_evaluator_test.cpp
)
target_link_libraries(neural_net_evaluator_tests engine_lib GTest::gtest_main "${TORCH_LIBRARIES}")
add_test(NAME neural_net_evaluator_tests COMMAND neural_net_evaluator_tests)
