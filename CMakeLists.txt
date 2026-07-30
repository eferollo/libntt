cmake_minimum_required(VERSION 3.20)

project(ntt_lib
    VERSION 0.0.1
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to an optimized build with debug symbols when no build type
# has been explicitly selected by the user.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo
        CACHE STRING "Build type" FORCE
    )
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug
    Release
    RelWithDebInfo
    MinSizeRel
)

option(NTT_BUILD_TESTS
    "Build the test suite"
    ON
)

option(NTT_BUILD_EXAMPLES
    "Build example programs"
    ON
)

option(NTT_ENABLE_LOGGING
    "Compile in NTT_LOG() calls (turn OFF for zero-overhead benchmark builds)"
    ON
)

install(FILES LICENSE DESTINATION share/licenses/libntt)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

add_subdirectory(src)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(NTT_BUILD_TESTS)
    enable_testing()

    # Require OpenSSL >= 3.0 for cryptographically secure random test data.
    find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

if(NTT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
