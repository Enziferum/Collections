cmake_minimum_required(VERSION 3.21)
project(Walli)

if(MSVC)
    add_definitions(/MP)
endif()


if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-source builds not allowed.
    Please make a new directory (called a build directory)
	and run CMake from there. You may need to remove CMakeCache.txt.")
endif()

cmake_policy(SET CMP0074 NEW)

set(CMAKE_CXX_STANDARD 17)

include(cmake/Base.cmake)

file(GLOB_RECURSE INCL include/*.hpp)
file(GLOB_RECURSE SRC source/*.cpp)

add_library(${PROJECT_NAME} STATIC ${INCL} ${SRC})
target_include_directories(${PROJECT_NAME} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

set(LIBS)
if (WALLI_USE_TWIST)
    set(LIBS twist)
endif ()

target_link_libraries(${PROJECT_NAME} ${LIBS})

if(WALLI_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(WALLI_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

if(WALLI_USE_TWIST)
    add_subdirectory(extlibs/twist)
endif()