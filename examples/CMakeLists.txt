# Deliberately skip exampples on Windows: the runtime module loader hard-codes 
# the POSIX shared-library convention (libntt_adapter_<name>.so), so the 
# "naive" module cannot be resolved from a .dll, and the -Wall/-Wextra/
# -Wpedantic flags below are rejected by MSVC.
if(WIN32)
    return()
endif()

# Third-party-style adapter module: links nothing against libntt, is compiled
# with hidden visibility, and reads the config through the injected core API.
ntt_adapter_module(naive adapters/naive/ntt_naive.c)

# Compiles a run-time loading example against the ntt shared library. Every
# example links the module target listed first, so the .so is rebuilt before
# the program runs, and defaults to loading modules from the build's module
# directory via the NTT_MODULE_DIR compile definition.
function(ntt_add_example module_target)
    foreach(example IN LISTS ARGN)
        add_executable(${example} ${example}.c)
        target_link_libraries(${example} PRIVATE ntt::ntt ${CMAKE_DL_LIBS})
        target_compile_definitions(${example} PRIVATE
            NTT_MODULE_DIR="${NTT_MODULE_OUTPUT_DIR}")
        add_dependencies(${example} ${module_target})
    endforeach()
endfunction()

# One-shot vs step-wise negacyclic multiplication. Both let the user pick the
# adapter on the command line (built-ins or the "naive" module) and default to
# the resolved library default adapter.
ntt_add_example(ntt_adapter_naive
    ntt_example_oneshot
    ntt_example_stepwise
)
