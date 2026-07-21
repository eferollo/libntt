#ifndef NTT_INTERNAL_H
#define NTT_INTERNAL_H

#include "ntt/ntt.h"
#include "ntt/ntt_log.h"

struct ntt_ctx_s {
    uint32_t q;            /* prime modulus */
    uint32_t n;            /* transform size, must be a power of 2 */
    uint32_t stages;       /* log2(n) number of NTT stages */
    uint32_t omega;        /* primitive n-th root of unity mod q */
    uint32_t omega_inv;    /* inverse of omega mod q */
    uint32_t n_inv;        /* inverse of n mod q */
    uint32_t psi;          /* primitive 2n-th root of unity mod q */
    uint32_t psi_inv;      /* inverse primitive of 2n-th root of unity mod q */
    uint32_t *psi_pow;     /* psi_pow[i] = psi^i mod q, i = 0..n-1 */
    uint32_t *psi_inv_pow; /* psi_inv_pow[i] = psi^-i mod q, i = 0..n-1 */
};

uint32_t ntt__addmod(uint32_t a, uint32_t b, uint32_t q);
uint32_t ntt__submod(uint32_t a, uint32_t b, uint32_t q);
uint32_t ntt__mulmod(uint32_t a, uint32_t b, uint32_t q);
uint32_t ntt__modpow(uint32_t base, uint32_t exp, uint32_t q);
uint32_t ntt__modinv(uint32_t a, uint32_t q);
int ntt__is_power_of_two(uint32_t x);
int ntt__bitrev_permute(uint32_t *a, uint32_t n);

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
