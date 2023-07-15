cmake_minimum_required(VERSION 3.16)
project(benchmark)

set(CMAKE_CXX_STANDARD 17)
set(SRC main.cpp benchmark.cpp)

add_executable(${PROJECT_NAME} ${SRC})
target_link_libraries(${PROJECT_NAME} Joker)