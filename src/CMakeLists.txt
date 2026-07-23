set(NTT_COMMON_SOURCES
    common/ntt_api.c
    common/ntt_adapter.c
    common/ntt_ctx.c
    common/ntt_config.c
    common/ntt_log.c
    common/ntt_utils.c
)

set(NTT_ADAPTER_SOURCES
    adapters/scalar_toy/ntt_arith.c
    adapters/scalar_toy/ntt_adapter.c
    adapters/scalar_toy/ntt.c
    # -- We'll add here future adapters here directly, no branching needed.
    #    Distinct ntt__<adapter>_* symbol names per backend are what
    #    makes listing them all unconditionally safe. CHECK THIS
)

add_library(ntt STATIC
    ${NTT_COMMON_SOURCES}
    ${NTT_ADAPTER_SOURCES}
)

target_include_directories(ntt
    PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common/headers
)

target_compile_options(ntt PRIVATE -Wall -Wextra -Wpedantic)

if(NTT_ENABLE_LOGGING)
    target_compile_definitions(ntt PUBLIC NTT_LOG_ENABLED)
endif()

add_library(ntt::ntt ALIAS ntt)
