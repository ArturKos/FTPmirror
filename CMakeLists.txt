cmake_minimum_required(VERSION 3.14)
project(ftp_mirror
    VERSION 1.0.0
    DESCRIPTION "FTP directory mirror tool built on raw BSD sockets"
    LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# Core library used by both the CLI and the test suite.
add_library(ftp_client STATIC src/ftp_client.c)
target_include_directories(ftp_client PUBLIC include)

# Command-line executable.
add_executable(ftp_mirror src/main.c)
target_link_libraries(ftp_mirror PRIVATE ftp_client)

# Tests.
option(FTP_MIRROR_BUILD_TESTS "Build Google Test suite" ON)
if(FTP_MIRROR_BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(ftp_client_tests tests/test_ftp_client.cc)
    target_link_libraries(ftp_client_tests PRIVATE ftp_client GTest::gtest GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(ftp_client_tests)
endif()

# Optional: Doxygen documentation target.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "FTP Mirror")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    doxygen_add_docs(docs
        include src
        COMMENT "Generating Doxygen documentation")
endif()
