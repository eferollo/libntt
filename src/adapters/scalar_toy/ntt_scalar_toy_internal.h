#ifndef NTT_SCALAR_TOY_INTERNAL_H
#define NTT_SCALAR_TOY_INTERNAL_H

#include "ntt_internal.h"
#include <stdint.h>

/**
 * @brief Private state owned by the scalar toy NTT adapter.
 *
 * The common NTT context intentionally does not expose these fields. This
 * adapter uses canonical uint32_t coefficients in [0, q) and computes all
 * modular arithmetic with generic scalar operations.
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
} ntt_scalar_toy_state;

uint32_t ntt__reduce(uint32_t a, uint32_t q);
uint32_t ntt__addmod(uint32_t a, uint32_t b, uint32_t q);
uint32_t ntt__submod(uint32_t a, uint32_t b, uint32_t q);
uint32_t ntt__mulmod(uint32_t a, uint32_t b, uint32_t q);
uint32_t ntt__modpow(uint32_t base, uint32_t exp, uint32_t q);
uint32_t ntt__modinv(uint32_t a, uint32_t q);

bool ntt__validate_modulus(uint32_t q);
void *ntt__adapter_setup(const ntt_config *config);
void ntt__adapter_teardown(void *state);

int ntt__forward(void *state, uint32_t *a);
int ntt__inverse(void *state, uint32_t *a);
int ntt__negacyclic_mul(void *state, uint32_t *a, uint32_t *b, uint32_t *c);

#endif /* NTT_SCALAR_TOY_INTERNAL_H */
