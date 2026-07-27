#ifndef NTT_INTERNAL_H
#define NTT_INTERNAL_H

#include "ntt/ntt.h"
#include "ntt_adapter.h"
#include "ntt_config.h"
#include <stdbool.h>
#include <stdlib.h>

/**
 * @brief Internal representation of an NTT context.
 *
 * The context contains only state that is common to every backend. All NTT
 * parameters, arithmetic state, lookup tables, and implementation-specific
 * data are owned by the selected backend and stored behind the opaque state
 * pointer.
 */
struct ntt_ctx_s {
    uint32_t q;                 /* prime modulus */
    uint32_t n;                 /* transform size, must be a power of 2 */
    const ntt_adapter *adapter; /* selected NTT adapter */
    void *state;                /* opaque backend-specific state */
};

bool ntt__is_power_of_two(uint32_t x);
int ntt__bitrev_permute(uint32_t *a, uint32_t n);
bool ntt__resolve_roots(uint32_t q,
                        uint32_t n,
                        ntt_transform_type type,
                        uint32_t *omega,
                        uint32_t *psi);

#define SAFE_FREE(ptr)       \
    do {                     \
        if ((ptr) != NULL) { \
            free(ptr);       \
            ptr = NULL;      \
        }                    \
    } while (0)

#define ZERO_STRUCTP(x)                   \
    do {                                  \
        if ((x) != NULL)                  \
            memset((x), 0, sizeof(*(x))); \
    } while (0)

#endif /* NTT_INTERNAL_H */
