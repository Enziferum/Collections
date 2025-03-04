# Some default variables which the user may change
option(CMAKE_BUILD_TYPE "Choose the type of build (Debug or Release)" Debug)

option(WALLI_USE_TSAN "Use tsan for multithreading testing" OFF)
option(WALLI_USE_TWIST "Use twist library for multithreading" OFF)
option(WALLI_BUILD_TESTS "Run tests" OFF)
option(WALLI_BUILD_SANDBOX "Run tests" OFF)


macro(process_logging_base_options)
    include(CMakePrintHelpers)
    message("Base Options: ")
    message("-----------------------------------------------")
    cmake_print_variables(USE_CMAKE_VERBOSE)
    cmake_print_variables(WALLI_BUILD_TESTS)
    cmake_print_variables(WALLI_BUILD_SANDBOX)
    cmake_print_variables(WALLI_USE_TSAN)
    cmake_print_variables(WALLI_USE_TWIST)
    message("-----------------------------------------------")
endmacro()
