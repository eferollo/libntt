#ifndef REDC_H
#define REDC_H

#include <stdint.h>

/* BARRETT REDUCTION */
uint64_t ntt_scalar_barrett_mu(uint32_t q);
uint32_t ntt_scalar_barrett_reduce_u64(uint64_t x, uint32_t q, uint64_t mu);
uint32_t
ntt_scalar_barrett_mul(uint32_t a, uint32_t b, uint32_t q, uint64_t mu);
uint32_t
ntt_scalar_barrett_modpow(uint32_t base, uint32_t exp, uint32_t q, uint64_t mu);

/* MONTGOMERY REDUCTION */
uint32_t ntt_scalar_mont_qinv(uint32_t q);
uint32_t ntt_scalar_mont_r2(uint32_t q);
uint32_t ntt_scalar_mont_reduce(uint64_t t, uint32_t q, uint32_t qinv);
uint32_t ntt_scalar_mont_mul(uint32_t a, uint32_t b, uint32_t q, uint32_t qinv);
uint32_t
ntt_scalar_mont_encode(uint32_t a, uint32_t q, uint32_t r2, uint32_t qinv);
uint32_t ntt_scalar_mont_decode(uint32_t a, uint32_t q, uint32_t qinv);
uint32_t ntt_scalar_mont_modpow(uint32_t base,
                                uint32_t exp,
                                uint32_t q,
                                uint32_t r2,
                                uint32_t qinv);

#endif /* REDC_H */
