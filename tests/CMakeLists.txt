# cmocka is required by the unit-test binaries.
find_package(cmocka REQUIRED)

# ntt_cmocka_unit(<name> SOURCES <src>... [INCLUDE_DIRS <dir>...])
#
# Builds a cmocka unit-test binary named <name> that:
#   - links the library (ntt::ntt) on top of cmocka and OpenSSL;
#   - exposes the internal implementation sources and headers
#   - registers itself with CTest under <name>.
#
# INCLUDE_DIRS carries extra include directories, typically a backend's
# private headers such as src/adapters/scalar/redc for the reduction tests.
function(ntt_cmocka_unit name)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDE_DIRS" ${ARGN})

    add_executable(${name}
        ${ARG_SOURCES}
    )

    target_link_libraries(${name}
        PRIVATE
            ntt::ntt
            cmocka::cmocka
            OpenSSL::Crypto
    )

    target_include_directories(${name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src/common
            ${CMAKE_SOURCE_DIR}/src/common/headers
            ${CMAKE_SOURCE_DIR}/tests/common
            ${ARG_INCLUDE_DIRS}
    )

    set_target_properties(${name}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    add_test(
        NAME ${name}
        COMMAND $<TARGET_FILE:${name}>
    )
endfunction()

add_subdirectory(unittests)
