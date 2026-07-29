#ifndef NTT_SCALAR_INTERNAL_H
#define NTT_SCALAR_INTERNAL_H

#include "ntt_internal.h"
#include "redc/redc.h"
#include "string.h"

/**
 * @brief Reduction backend used by the optimized scalar adapter.
 *
 * Note:
 *   - Barrett mode keeps values in canonical representation.
 *   - Montgomery mode stores internal values as xR mod q with R=2^{32}.
 */
typedef enum {
    /* Use reciprocal-based Barrett redction. */
    NTT_SCALAR_REDUCTION_BARRETT = 0,
    /* Use Montgomery REDC for odd moduli. */
    NTT_SCALAR_REDUCTION_MONTGOMERY = 1,
} ntt_scalar_redc;

/**
 * @brief Private state owned by the optimized scalar NTT adapter.
 *
 * Stores the configured NTT parameters, reduction-specific constants, and
 * precomputed twiddle/twist tables. The representation of all precomputed
 * arithmetic tables follows @ref ntt_scalar_reduction so that the transform
 * hot path can operate without conversions.
 */
typedef struct {
    uint32_t q;            /* prime modulus */
    uint32_t n;            /* transform size */
    uint32_t stages;       /* log2(n) number of NTT stages */
    uint32_t omega;        /* primitive n-th root of unity mod q */
    uint32_t omega_inv;    /* inverse of omega mod q */
    uint32_t n_inv;        /* inverse of n mod q */
    uint32_t psi;          /* primitive 2n-th root of unity mod q */
    uint32_t psi_inv;      /* inverse primitive 2n-th root of unity mod q */
    uint32_t *psi_pow;     /* psi_pow[i] = psi^i mod q, i = 0..n-1 */
    uint32_t *psi_inv_pow; /* psi_inv_pow[i] = psi^-i mod q, i = 0..n-1 */

    ntt_scalar_redc reduction; /* selected reduction mode */

    uint64_t barrett_mu; /* Barrett: floor(2^32 / q) */

    uint32_t mont_qinv; /* Montgomery: qinv = -q^(-1) mod R */
    uint32_t mont_r2;   /* Montgomery: R^2 mod q (R = 2^32) */

    uint32_t *fwd_twiddle; /* pre-computed stage forward twiddles */
    uint32_t *inv_twiddle; /* pre-computed stage inverse twiddles */
} ntt_scalar_state;

bool ntt__scalar_validate_modulus(const ntt_config *config);
void *ntt__scalar_adapter_setup(const ntt_config *config);
void ntt__scalar_adapter_teardown(void *state);

int ntt__scalar_forward(void *state, uint32_t *a);
int ntt__scalar_inverse(void *state, uint32_t *a);
int ntt__scalar_negacyclic_mul(void *state,
                               uint32_t *a,
                               uint32_t *b,
                               uint32_t *c);

uint32_t ntt__scalar_canonicalize_value(uint32_t a,
                                        const ntt_scalar_state *state);
uint32_t ntt__scalar_mul(uint32_t a, uint32_t b, const ntt_scalar_state *state);
uint32_t ntt__scalar_add(uint32_t a, uint32_t b, const ntt_scalar_state *state);
uint32_t ntt__scalar_sub(uint32_t a, uint32_t b, const ntt_scalar_state *state);
uint32_t ntt__scalar_encode_value(uint32_t a, const ntt_scalar_state *state);
uint32_t ntt__scalar_decode_value(uint32_t a, const ntt_scalar_state *state);
uint32_t
ntt__scalar_modpow(uint32_t base, uint32_t exp, const ntt_scalar_state *state);

#endif /* NTT_SCALAR_INTERNAL_H */
