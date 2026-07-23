#include "ntt_scalar_toy_internal.h"

/**
 * @brief Returns the scalar toy NTT adapter descriptor.
 *
 * The scalar toy adapter is the generic reference implementation of the NTT
 * library. Its complete implementation, including modular arithmetic,
 * transform parameters, lookup tables, and temporary state, is owned by the
 * adapter and hidden behind the opaque adapter state pointer.
 *
 * @return Pointer to the scalar toy adapter descriptor.
 */
const ntt_adapter *ntt_adapter_scalar_toy(void)
{
    static const ntt_adapter adapter = {
        .abi_version = NTT_ADAPTER_ABI_VERSION,
        .struct_size = sizeof(ntt_adapter),
        .name = "scalar_toy",
        .capabilities = NTT_CAP_RUNTIME_MODULUS,
        .supported_flags = 0,
        .validate_modulus = ntt__validate_modulus,
        .setup = ntt__adapter_setup,
        .teardown = ntt__adapter_teardown,
        .forward = ntt__forward,
        .inverse = ntt__inverse,
        .negacyclic_mul = ntt__negacyclic_mul,
    };

    return &adapter;
}
