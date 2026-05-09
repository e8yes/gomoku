cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(test_torch)

find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

add_executable(test_torch test_torch.cpp)
target_link_libraries(test_torch "${TORCH_LIBRARIES}")
set_property(TARGET test_torch PROPERTY CXX_STANDARD 17)
