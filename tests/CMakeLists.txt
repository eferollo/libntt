# cmocka is required by the unit-test binaries.
find_package(cmocka REQUIRED)

# Unlike examples, unit tests are allowed to access the library's private
# implementation headers. This allows them to inspect and validate internal
# structures and backend-specific functionality that is intentionally hidden
# from normal library users.
#
# ntt_cmocka_unit(<name> <source> [<extra_includes>...])
#   Builds a cmocka unit-test binary that:
#     - links ntt::ntt, cmocka::cmocka and OpenSSL::Crypto;
#     - gets the shared internal headers dir;
#     - registers itself with CTest under <name>.
function(ntt_cmocka_unit name source)
    add_executable(${name}
        ${source}
    )

    target_link_libraries(${name}
        PRIVATE
            ntt::ntt
            cmocka::cmocka
            OpenSSL::Crypto
    )

    target_include_directories(${name}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src/common/headers
            ${PROJECT_SOURCE_DIR}/tests/common
            ${ARGN}
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

ntt_cmocka_unit(torture_scalar_barrett
    unittests/torture_scalar_barrett.c
    ${PROJECT_SOURCE_DIR}/src/adapters/scalar/redc
)

ntt_cmocka_unit(torture_scalar_montgomery
    unittests/torture_scalar_montgomery.c
    ${PROJECT_SOURCE_DIR}/src/adapters/scalar/redc
)

# Like ntt_cmocka_unit() but does NOT link ntt::ntt. Used for tests that
# #include a library .c file directly so its private # static helpers are 
# individually testable. The included source's non-static symbols would
# otherwise collide with the copies compiled into the library.
#
# The real logging implementation (common/ntt_log.c) is linked in and
# NTT_LOG_ENABLED is defined, so NTT_LOG() works inside the included source
# and the test can steer it with ntt_log_set_level(). Pass WITH_LOG=OFF for
# tests that #include ntt_log.c themselves (torture_ntt_log) to avoid
# duplicate definitions.
function(ntt_cmocka_unit_standalone name source)
    cmake_parse_arguments(ARG "" "WITH_LOG" "" ${ARGN})
    if(NOT DEFINED ARG_WITH_LOG)
        set(ARG_WITH_LOG ON)
    endif()

    set(_srcs ${source})
    if(ARG_WITH_LOG)
        list(APPEND _srcs ${PROJECT_SOURCE_DIR}/src/common/ntt_log.c)
    endif()

    add_executable(${name} ${_srcs})

    target_link_libraries(${name}
        PRIVATE
            cmocka::cmocka
    )

    target_include_directories(${name}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src/common/headers
            ${PROJECT_SOURCE_DIR}/src/common
            ${PROJECT_SOURCE_DIR}/tests/common
            ${ARGN}
    )

    target_compile_definitions(${name}
        PRIVATE
            NTT_LOG_ENABLED
    )

    set_target_properties(${name}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/unittests
    )

    add_test(
        NAME ${name}
        COMMAND $<TARGET_FILE:${name}>
    )
endfunction()

ntt_cmocka_unit_standalone(torture_ntt_utils
    unittests/torture_ntt_utils.c
)

ntt_cmocka_unit_standalone(torture_ntt_log
    unittests/torture_ntt_log.c
    WITH_LOG OFF
)
