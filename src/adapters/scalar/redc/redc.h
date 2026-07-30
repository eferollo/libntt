/*
 * redc.h
 * This file is part of the NTT Library.
 *
 * Copyright 2026 Francesco Rollo <eferollo@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
