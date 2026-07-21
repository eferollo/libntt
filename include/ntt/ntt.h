#ifndef NTT_H
#define NTT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ntt_ctx_s ntt_ctx;

typedef enum {
    NTT_OK = 0,
    NTT_ERROR = -1,
} ntt_status;

/**
 * @brief Initialize an NTT context structure.
 *
 * @param[in] q         Prime modulus.
 * @param[in] n         Transform size (power of two).
 * @param[in] omega     A primitive n-th root of unity mod q.
 * @param[in] psi       A primitive 2n-th root of unity mod q, with
 *                      psi^2 = omega.
 *
 * @return Newly allocated context, or NULL on invalid parameters or
 *         allocation failure.
 */
ntt_ctx *ntt_create(uint32_t q, uint32_t n, uint32_t omega, uint32_t psi);

/**
 * @brief Releases all resources owned by an NTT context.
 *
 * Frees the dynamically allocated lookup tables stored in the context,
 * securely clears the context structure, and leaves no sensitive data
 * behind.
 *
 * @param[in] ctx   Pointer to the NTT context to be released.
 */
void ntt_destroy(ntt_ctx *ctx);

int ntt_negacyclic_mul(uint32_t *a,
                       uint32_t *b,
                       uint32_t *c,
                       const ntt_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NTT_H */
