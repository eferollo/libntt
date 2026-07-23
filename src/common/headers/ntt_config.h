#ifndef NTT_CONFIG_INTERNAL_H
#define NTT_CONFIG_INTERNAL_H

#include "ntt/ntt_adapter.h"

/**
 * @brief Internal representation of the opaque public NTT configuration object.
 */
struct ntt_config_s {
    uint32_t q;
    uint32_t n;
    uint32_t omega;
    uint32_t psi;
    uint32_t flags;
};

#endif /* NTT_CONFIG_INTERNAL_H */
