#include "ntt_scalar_internal.h"

/**
 * @brief Returns the optimized scalar NTT adapter.
 *
 * The adapter implements radix-2 Cooley-Tukey forward NTT and radix-2
 * Gentleman-Sande inverse NTT. Barrett reduction is the default arithmetic
 * mode. Montgomery reduction can be requested with
 * NTT_CONFIG_REDUCTION_MONTGOMERY.
 *
 * @return Pointer to the scalar adapter descriptor.
 */
const ntt_adapter *ntt_adapter_scalar(void)
{
    static const ntt_adapter adapter = {
        .abi_version = NTT_ADAPTER_ABI_VERSION,
        .struct_size = sizeof(ntt_adapter),
        .name = "scalar",
        .capabilities =
            NTT_CAP_RUNTIME_MODULUS | NTT_CAP_BARRETT | NTT_CAP_MONTGOMERY,
        .supported_flags = NTT_CONFIG_REDUCTION_BARRETT |
                           NTT_CONFIG_REDUCTION_MONTGOMERY,
        .validate_modulus = ntt__scalar_validate_modulus,
        .setup = ntt__scalar_adapter_setup,
        .teardown = ntt__scalar_adapter_teardown,
        .forward = ntt__scalar_forward,
        .inverse = ntt__scalar_inverse,
        .negacyclic_mul = ntt__scalar_negacyclic_mul,
    };

    return &adapter;
}
