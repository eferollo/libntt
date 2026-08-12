project(unittests C)

set(NTT_UNIT_TESTS
    torture_ntt_adapter
    torture_ntt_api
    torture_ntt_cfg_file
    torture_ntt_config
    torture_ntt_core
    torture_ntt_ctx
    torture_ntt_log
    torture_ntt_utils
    torture_scalar_barrett
    torture_scalar_montgomery
)

# Reduction-backend tests that additionally need the private redc/ headers.
set(NTT_UNIT_TESTS_REDC
    torture_scalar_barrett
    torture_scalar_montgomery
)

foreach(_UNIT_TEST ${NTT_UNIT_TESTS})
    set(_extra_includes "")
    if(_UNIT_TEST IN_LIST NTT_UNIT_TESTS_REDC)
        list(APPEND _extra_includes
            ${CMAKE_SOURCE_DIR}/src/adapters/scalar/redc)
    endif()

    ntt_cmocka_unit(${_UNIT_TEST}
        SOURCES ${_UNIT_TEST}.c
        INCLUDE_DIRS ${_extra_includes}
    )
endforeach()
