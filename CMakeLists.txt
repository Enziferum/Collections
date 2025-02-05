cmake_minimum_required(VERSION 3.21)
project(Collections)

if(MSVC)
    add_definitions(/MP)
endif()


if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-source builds not allowed.
    Please make a new directory (called a build directory)
	and run CMake from there. You may need to remove CMakeCache.txt.")
endif()

cmake_policy(SET CMP0074 NEW)

if(RB2D_USE_CMAKE_VERBOSE)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif ()

set(CMAKE_CXX_STANDARD 17)

include(cmake/Base.cmake)

file(GLOB_RECURSE INCL include/*.hpp)
file(GLOB_RECURSE SRC source/*.cpp)

add_library(${PROJECT_NAME} STATIC ${INCL} ${SRC})
target_include_directories(${PROJECT_NAME} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

set(USE_TSAN OFF)
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

if(BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

if(USE_TWIST)
    add_subdirectory(extlibs/twist)
endif()