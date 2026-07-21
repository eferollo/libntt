cmake_minimum_required(VERSION 3.20)
project(ntt_lib C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NTT_BUILD_TESTS "Build the test suite" ON)
option(NTT_BUILD_EXAMPLES "Build example programs" ON)
option(NTT_BUILD_ALL_BACKENDS "Build every available backend as its own library" OFF)
option(NTT_ENABLE_LOGGING "Compile in NTT_LOG() calls (turn OFF for zero-overhead benchmark builds)" ON)

set(NTT_DEFAULT_BACKEND "scalar_toy" CACHE STRING
    "Backend that ntt::default aliases (scalar_toy, ...)")

add_subdirectory(src)

if(NTT_BUILD_TESTS)
    enable_testing()
    # Require OpenSSL>=3.0
    find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
    add_subdirectory(tests)
endif()

if(NTT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
