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

    add_test(
        NAME ${name}
        COMMAND ${name}
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