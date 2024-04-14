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

set(USE_TSAN ON)
if (USE_TSAN)

    message(STATUS "Sanitize with Thread Sanitizer (TSAN)")
    set(TSAN_COMPILE_FLAGS -fsanitize=thread -fno-sanitize-recover=all)
    set(TSAN_LINK_FLAGS -fsanitize=thread)

    target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=thread)
    target_link_options(${PROJECT_NAME}  PRIVATE -fsanitize=thread)
endif ()

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