cmake_minimum_required(VERSION 3.20)

project(libntt
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

# Shared libraries emitted for the run-time loading examples go into the
# examples build directory.
set(NTT_MODULE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/examples/modules")

# Adapter module helper.
#
# Builds a standalone shared library that implements an NTT adapter and links
# nothing against libntt. The module sees only the public headers, exports a
# single ntt_adapter_module_init entry point, and reads the configuration
# through the ntt_core_api dispatched by the loader at run time.
function(ntt_adapter_module name)
    add_library(ntt_adapter_${name} SHARED ${ARGN})
    set_target_properties(ntt_adapter_${name} PROPERTIES
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME "ntt_adapter_${name}"
        LIBRARY_OUTPUT_DIRECTORY ${NTT_MODULE_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${NTT_MODULE_OUTPUT_DIR})
    target_include_directories(ntt_adapter_${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ntt_adapter_${name} PRIVATE
        NTT_MODULE_BUILD)
    target_compile_options(ntt_adapter_${name} PRIVATE
        -Wall -Wextra -Wpedantic)
endfunction()

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
