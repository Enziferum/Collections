cmake_minimum_required(VERSION 3.16)
project(Joker)

option(BUILD_TESTS "Build Tests?" ON)
option(BUILD_BENCHMARK "Build Benchmark?" ON)
option(BUILD_SANDBOX "Build Sandbox?" ON)
option(USE_TWIST "Use Twist library?" ON)

set(CMAKE_CXX_STANDARD 17)
file(GLOB_RECURSE SRC source/*.cpp)

add_library(${PROJECT_NAME} ${SRC})
target_include_directories(${PROJECT_NAME} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(${PROJECT_NAME} twist)


if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARK)
    add_subdirectory(benchmark)
endif()

if(BUILD_BENCHMARK)
    add_subdirectory(sandbox)
endif()

if(USE_TWIST)
    add_subdirectory(extlibs/twist)
endif()