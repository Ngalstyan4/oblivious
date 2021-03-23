cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(example-app)

find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

add_executable(infer infer.cpp)
add_executable(infer20 infer20.cpp)
target_link_libraries(infer "${TORCH_LIBRARIES}")
target_link_libraries(infer20 "${TORCH_LIBRARIES}")
set_property(TARGET infer PROPERTY CXX_STANDARD 14)
set_property(TARGET infer20 PROPERTY CXX_STANDARD 14)
