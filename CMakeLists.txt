cmake_minimum_required(VERSION 3.21)
project(Collections)

if(MSVC)
    add_definitions(/MP)
endif()

option(BUILD_TESTS "Build Tests?" ON)
option(BUILD_SANDBOX "Build Sandbox?" ON)

option(BUILD_BENCHMARK "Build Benchmark?" OFF)
option(USE_TWIST "Use Twist library?" OFF)

set(CMAKE_CXX_STANDARD 17)
file(GLOB_RECURSE INCL include/*.hpp)
file(GLOB_RECURSE SRC source/*.cpp)

add_library(${PROJECT_NAME} ${INCL} ${SRC})
target_include_directories(${PROJECT_NAME} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

set(LIBS)
if (USE_TWIST)
    set(LIBS twist)
endif ()

target_link_libraries(${PROJECT_NAME} ${LIBS})

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARK)
    add_subdirectory(benchmark)
endif()

if(BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

if(USE_TWIST)
    add_subdirectory(extlibs/twist)
endif()