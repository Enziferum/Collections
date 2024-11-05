# Some default variables which the user may change
option(CMAKE_BUILD_TYPE "Choose the type of build (Debug or Release)" Debug)
option(BUILD_TESTS "Build Tests?" ON)
option(BUILD_SANDBOX "Build Sandbox?" ON)
option(USE_TSAN "Use Tsan sanitazer" OFF)
option(USE_TWIST "Use Twist library?" OFF)


macro(process_logging_base_options)
    include(CMakePrintHelpers)
    message("Base Options: ")
    message("-----------------------------------------------")
    cmake_print_variables(USE_CMAKE_VERBOSE)
    cmake_print_variables(BUILD_TESTS)
    cmake_print_variables(BUILD_SANDBOX)
    cmake_print_variables(USE_TSAN)
    cmake_print_variables(USE_TWIST)
    message("-----------------------------------------------")
endmacro()
