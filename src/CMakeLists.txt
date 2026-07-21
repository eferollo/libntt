set(NTT_COMMON_SOURCES
    common/ntt_arith.c
    common/ntt_ctx.c
    common/ntt_log.c
)

# add_ntt_backend(name source1 [source2 ...])
#
# Creates a static library `ntt_<name>` implementing the full public API
# (include/ntt/ntt.h) by combining the shared common sources with the
# backend-specific sources passed in. Every backend therefore exposes
# identical symbol names (ntt_create, ntt_forward, ...), which is exactly
# what lets tests/examples link against ntt::<name> without caring which
# backend it is. The flip side: two backends can never be linked into the
# same binary at once (symbol clash by design).
function(add_ntt_backend name)
    set(target ntt_${name})
    add_library(${target} STATIC
        ${NTT_COMMON_SOURCES}
        ${ARGN}
    )
    target_include_directories(${target}
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/common
    )
    target_compile_options(${target} PRIVATE -Wall -Wextra)
    if(NTT_ENABLE_LOGGING)
        target_compile_definitions(${target} PUBLIC NTT_LOG_ENABLED)
    endif()
    add_library(ntt::${name} ALIAS ${target})
endfunction()

add_ntt_backend(scalar_toy
    scalar_toy/ntt_scalar_toy.c
)

# add_ntt_backend(scalar
#     scalar/ntt_scalar.c
# )
#

if(NOT TARGET ntt_${NTT_DEFAULT_BACKEND})
    message(FATAL_ERROR "NTT_DEFAULT_BACKEND=${NTT_DEFAULT_BACKEND} doesn't exist")
endif()
add_library(ntt::default ALIAS ntt_${NTT_DEFAULT_BACKEND})


