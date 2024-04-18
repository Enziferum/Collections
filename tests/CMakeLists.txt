add_subdirectory(concurrency)
add_subdirectory(rstd)
add_subdirectory(queues)
add_subdirectory(util)

cmake_policy(SET CMP0135 NEW)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY "https://github.com/google/googletest"
        GIT_TAG        main
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

set(TESTS_NAME run_all_tests)
set(SRC ${RSTL_TESTS_SRC} ${CONCURRENCY_TESTS_SRC} main.cpp)

add_executable(${TESTS_NAME} ${SRC})
target_link_libraries(${TESTS_NAME} PUBLIC GTest::gtest_main GTest::gmock_main PRIVATE Collections)

include(GoogleTest)
gtest_discover_tests(${TESTS_NAME})