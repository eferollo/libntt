# cmocka is required by the unit-test binaries.
find_package(cmocka REQUIRED)

# Unlike examples, tests are allowed to access the library's private
# implementation headers. This allows tests to inspect and validate internal
# structures and backend-specific functionality that is intentionally hidden
# from normal library users.
add_executable(torture_ntt
    torture_ntt.c
)

# Link the test against the NTT library and OpenSSL.
target_link_libraries(torture_ntt
    PRIVATE
        ntt::ntt
        OpenSSL::Crypto
)

# Allow the test to include both the public API headers and private internal
# headers.
#
# The public headers are exposed through the ntt::ntt target itself, while
# the internal headers are explicitly added here and are therefore visible
# only to this test target.
target_include_directories(torture_ntt
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/common/headers
)

# Register the torture test with CTest.
add_test(
    NAME torture_ntt
    COMMAND torture_ntt
)

# Unit tests
add_executable(torture_scalar_barrett
    unittests/torture_scalar_barrett.c
)

target_link_libraries(torture_scalar_barrett
    PRIVATE
        ntt::ntt
        cmocka::cmocka
        OpenSSL::Crypto
)

target_include_directories(torture_scalar_barrett
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/common/headers
        ${PROJECT_SOURCE_DIR}/src/adapters/scalar/redc
)

# Register the Barrett unit tests with CTest.
add_test(
    NAME torture_scalar_barrett
    COMMAND torture_scalar_barrett
)
