cmake_minimum_required(VERSION 3.21)
project(sandbox)

set(CMAKE_CXX_STANDARD 17)
set(SRC main.cpp benchmark.cpp)

set(USE_TSAN ON)


set(TSAN_COMPILE_FLAGS -fsanitize=thread -fno-sanitize-recover=all)
set(TSAN_LINK_FLAGS -fsanitize=thread)

if(USE_TSAN)
    add_compile_options(${TSAN_COMPILE_FLAGS})
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TSAN_LINK_FLAGS}")
endif()

add_executable(${PROJECT_NAME} ${SRC})
target_link_libraries(${PROJECT_NAME} PRIVATE Collections)