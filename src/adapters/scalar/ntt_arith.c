/*
 * ntt_arith.c
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

#include "ntt_scalar_internal.h"

/**
 * @brief Adds two backend-domain residues modulo the scalar context modulus.
 *
 * Addition is representation-preserving: canonical Barrett values remain
 * canonical and Montgomery-domain values remain in Montgomery form.
 *
 * @param[in] a First residue in the current backend representation.
 * @param[in] b Second residue in the current backend representation.
 * @param[in] q Modulus.
 *
 * @return Backend-domain sum modulo @p q.
 */
static uint32_t scalar_add(uint32_t a, uint32_t b, uint32_t q)
{
    uint64_t sum = (uint64_t)a + b;
    return sum >= q ? (uint32_t)(sum - q) : (uint32_t)sum;
}

/**
 * @brief Subtracts two backend-domain residues modulo the scalar context
 * modulus.
 *
 * Subtraction is representation-preserving for both Barrett and Montgomery
 * arithmetic domains.
 *
 * @param[in] a Minuend in the current backend representation.
 * @param[in] b Subtrahend in the current backend representation.
 * @param[in] q Modulus.
 *
 * @return Backend-domain difference modulo @p q.
 */
static uint32_t scalar_sub(uint32_t a, uint32_t b, uint32_t q)
{
    return a >= b ? a - b : (uint32_t)((uint64_t)a + q - b);
}

/**
 * @brief Reduces an externally supplied integer into canonical representation.
 *
 * Barrett mode directly reduces the input with its precomputed reciprocal.
 * Montgomery mode interprets the input as an ordinary integer and therefore
 * canonicalizes it through an encode/decode round trip. This function is used
 * at API and setup boundaries, not inside the NTT butterfly hot path.
 *
 * @param[in] a      External integer value.
 * @param[in] state  Scalar adapter state.
 *
 * @return Canonical residue in @f$[0,q)@f$.
 */
static uint32_t scalar_reduce(uint32_t a, const ntt_scalar_state *state)
{
    if (state->reduction == NTT_SCALAR_REDUCTION_MONTGOMERY) {
        uint32_t a_enc = ntt_scalar_mont_encode(a,
                                                state->q,
                                                state->mont_r2,
                                                state->mont_qinv);
        return ntt_scalar_mont_decode(a_enc, state->q, state->mont_qinv);
    }
    return ntt_scalar_barrett_reduce_u64(a, state->q, state->barrett_mu);
}

/**
 * @brief Dispatches modular multiplication to the selected reduction backend.
 *
 * Barrett operands are canonical residues. Montgomery operands must already
 * be encoded in Montgomery representation.
 *
 * @param[in] a     First backend-domain operand.
 * @param[in] b     Second backend-domain operand.
 * @param[in] state Scalar adapter state selecting the reduction backend.
 * @return Backend-domain product modulo @p q.
 */
static uint32_t
scalar_mul(uint32_t a, uint32_t b, const ntt_scalar_state *state)
{
    if (state->reduction == NTT_SCALAR_REDUCTION_MONTGOMERY) {
        return ntt_scalar_mont_mul(a, b, state->q, state->mont_qinv);
    }
    return ntt_scalar_barrett_mul(a, b, state->q, state->barrett_mu);
}

/**
 * @brief Encodes a canonical value for the selected arithmetic backend.
 *
 * Barrett mode keeps canonical representation. Montgomery mode converts the
 * value to @f$aR\bmod q@f$.
 *
 * @param[in] a     Canonical input value.
 * @param[in] state Scalar adapter state.
 *
 * @return Value in the selected backend representation.
 */
static uint32_t scalar_encode(uint32_t a, const ntt_scalar_state *state)
{
    if (state->reduction == NTT_SCALAR_REDUCTION_MONTGOMERY) {
        return ntt_scalar_mont_encode(a,
                                      state->q,
                                      state->mont_r2,
                                      state->mont_qinv);
    }
    return scalar_reduce(a, state);
}

/**
 * @brief Decodes an internal backend value to canonical representation.
 *
 * Barrett values are already canonical. Montgomery values are converted with
 * one REDC operation to remove the Montgomery factor.
 *
 * @param[in] a     Backend-domain value.
 * @param[in] state Scalar adapter state.
 *
 * @return Canonical residue in @f$[0,q)@f$.
 */
static uint32_t scalar_decode(uint32_t a, const ntt_scalar_state *state)
{
    if (state->reduction == NTT_SCALAR_REDUCTION_MONTGOMERY) {
        return ntt_scalar_mont_decode(a, state->q, state->mont_qinv);
    }
    return scalar_reduce(a, state);
}

/**
 * @brief Canonicalizes an externally supplied integer value.
 *
 * Unlike @ref ntt_scalar_encode_value, this function never treats the input as
 * an already encoded Montgomery value. It is intended for public API inputs
 * and setup-time parameters that are ordinary integers.
 *
 * @param[in] a     External integer value.
 * @param[in] state Scalar adapter state.
 *
 * @return Canonical residue in @f$[0,q)@f$.
 */
uint32_t ntt__scalar_canonicalize_value(uint32_t a,
                                        const ntt_scalar_state *state)
{
    return scalar_reduce(a, state);
}

/**
 * @brief Multiplies two values using the selected reduction backend.
 *
 * Operands must be in the representation expected by the selected backend.
 *
 * @param[in] a     First backend-domain operand.
 * @param[in] b     Second backend-domain operand.
 * @param[in] state Scalar adapter state.
 *
 * @return Backend-domain product modulo @p q.
 */
uint32_t ntt__scalar_mul(uint32_t a, uint32_t b, const ntt_scalar_state *state)
{
    return scalar_mul(a, b, state);
}

/**
 * @brief Adds two residues modulo the context modulus.
 *
 * The operation preserves the current arithmetic representation and therefore
 * is valid for both canonical Barrett values and Montgomery-domain values.
 *
 * @param[in] a     First residue.
 * @param[in] b     Second residue.
 * @param[in] state Scalar adapter state.
 *
 * @return Backend-domain sum modulo @p q.
 */
uint32_t ntt__scalar_add(uint32_t a, uint32_t b, const ntt_scalar_state *state)
{
    return scalar_add(a, b, state->q);
}

/**
 * @brief Subtracts two residues modulo the context modulus.
 *
 * The operation preserves the current arithmetic representation and therefore
 * is valid for both canonical Barrett values and Montgomery-domain values.
 *
 * @param[in] a     Minuend.
 * @param[in] b     Subtrahend.
 * @param[in] state Scalar adapter state.
 *
 * @return Backend-domain difference modulo @p q.
 */
uint32_t ntt__scalar_sub(uint32_t a, uint32_t b, const ntt_scalar_state *state)
{
    return scalar_sub(a, b, state->q);
}

/**
 * @brief Encodes a canonical value for internal backend arithmetic.
 *
 * @param[in] a     Canonical input value.
 * @param[in] state Scalar adapter state.
 *
 * @return Backend-domain representation of @p a.
 */
uint32_t ntt__scalar_encode_value(uint32_t a, const ntt_scalar_state *state)
{
    return scalar_encode(a, state);
}

/**
 * @brief Decodes an internal backend value into canonical representation.
 *
 * @param[in] a     Backend-domain input value.
 * @param[in] state Scalar adapter state.
 *
 * @return Canonical residue.
 */
uint32_t ntt__scalar_decode_value(uint32_t a, const ntt_scalar_state *state)
{
    return scalar_decode(a, state);
}

/**
 * @brief Computes modular exponentiation using the selected reduction backend.
 *
 * Dispatches to the dedicated Barrett or Montgomery implementation while
 * exposing a common canonical-input/canonical-output interface to setup code.
 *
 * @param[in] base  Canonical base value.
 * @param[in] exp   Non-negative exponent.
 * @param[in] state Scalar adapter state.
 *
 * @return Canonical residue @f$base^{exp}\bmod q@f$.
 */
uint32_t
ntt__scalar_modpow(uint32_t base, uint32_t exp, const ntt_scalar_state *state)
{
    if (state->reduction == NTT_SCALAR_REDUCTION_MONTGOMERY) {
        return ntt_scalar_mont_modpow(base,
                                      exp,
                                      state->q,
                                      state->mont_r2,
                                      state->mont_qinv);
    }
    return ntt_scalar_barrett_modpow(base, exp, state->q, state->barrett_mu);
}
