include(../cmake/External_GTest.cmake)
include_directories(${GTEST_INCLUDE_DIRS})
enable_testing()

add_subdirectory(concurrency)
add_subdirectory(rstl)

set(TESTS_NAME run_all_tests)
set(SRC ${RSTL_TESTS_SRC} ${CONCURRENCY_TESTS_SRC} main.cpp)

add_executable(${TESTS_NAME} ${SRC})
target_link_libraries(${TESTS_NAME} gtest Joker)

add_dependencies(${TESTS_NAME} gtest_ext)
add_test(NAME unit_tests COMMAND ${TESTS_NAME})