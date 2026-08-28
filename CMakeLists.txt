cmake_minimum_required(VERSION 3.25)

project(
    BTD5ModLoader
    VERSION 0.1.0
    DESCRIPTION "A Lua mod loader for the 32-bit Steam release of Bloons TD 5"
    LANGUAGES C CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "BTD5 Mod Loader currently supports Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The native loader must be configured for Win32/x86.")
endif()

option(BTD5ML_BUILD_TESTS "Build native tests" ON)
option(BTD5ML_ENABLE_ANALYSIS "Enable MSVC static analysis" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)
btd5ml_enable_runtime_dependencies()

add_subdirectory(src/native)

if(BTD5ML_BUILD_TESTS)
    include(CTest)
    btd5ml_enable_test_dependencies()
    add_subdirectory(tests/native)
endif()
