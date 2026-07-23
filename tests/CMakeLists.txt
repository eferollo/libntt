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
