/*
 * ntt.c
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

#define NTT_SCALAR_Q32_MAX UINT32_MAX

/**
 * @brief Applies an in-place bit-reversal permutation using a precomputed
 *        lookup table.
 *
 * Reorders an array according to the supplied bit-reversal table, where
 * @p table[i] contains the bit-reversed index of @p i. Using a cached
 * permutation table avoids recomputing bit-reversed indices for every
 * transform and reduces the overhead of repeated NTT executions.
 *
 * @param[in,out] a     Array to permute.
 * @param[in] table     Precomputed bit-reversal table.
 * @param[in] n         Number of elements in @p a.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR if an input pointer is NULL.
 */
static int
scalar_bitrev_permute_cached(uint64_t *a, const uint32_t *table, uint32_t n)
{
    if (a == NULL || table == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }

    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = table[i];
        if (i < j) {
            uint64_t tmp = a[i];
            a[i] = a[j];
            a[j] = tmp;
        }
    }

    return NTT_OK;
}

/**
 * @brief Computes the multiplicative inverse of a non-zero field element.
 *
 * Uses Fermat's little theorem, @f$a^{-1}=a^{q-2}\bmod q@f$, which is valid
 * because the scalar adapter is intended for prime NTT moduli. The selected
 * reduction backend is used for every multiplication during exponentiation.
 *
 * @param a     Non-zero canonical field element.
 * @param state Scalar adapter state containing the prime modulus.
 *
 * @return Canonical modular inverse of @p a.
 */
static uint64_t scalar_modinv(uint64_t a, const ntt_scalar_state *state)
{
    return ntt__scalar_modpow(a, state->q - 2, state);
}

/**
 * @brief Raises an NTT root to a power using the selected backend.
 *
 * This helper centralizes root-table generation so setup uses exactly the
 * same Barrett or Montgomery multiplication primitives as the transform
 * itself.
 *
 * @param root     Canonical root of unity.
 * @param exponent Non-negative exponent.
 * @param state    Scalar adapter state.
 *
 * @return Canonical value @f$root^{exponent}\bmod q@f$.
 */
static uint64_t scalar_root_power(uint64_t root,
                                  uint64_t exponent,
                                  const ntt_scalar_state *state)
{
    return ntt__scalar_modpow(root, exponent, state);
}

/**
 * @brief Validates the modulus requirements of the scalar adapter.
 *
 * The scalar adapter operates over the prime field Z_q. This callback
 * verifies that q is prime through ntt_is_prime() and that q does not exceed
 * 2^63, the supported range of the general 64-bit reduction path.
 *
 * When Montgomery REDC is requested, q must additionally be odd so that
 * gcd(q, R) = 1 and q is therefore invertible modulo R.
 *
 * Generic transform requirements involving both q and n are checked by the
 * common NTT context layer before root resolution.
 *
 * @param[in] config NTT configuration.
 *
 * @return true if config's q satisfies the scalar adapter's modulus
 *         requirements for the selected reduction backend.
 * @return false otherwise.
 */
bool ntt__scalar_validate_modulus(const ntt_config *config)
{
    uint64_t q;
    uint32_t flags;

    if (config == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid NTT configuration");
        return false;
    }

    q = ntt_config_get_modulus(config);
    flags = ntt_config_get_flags(config);

    /*
     * Reject moduli greater than 2^63. The general 64-bit Barrett and
     * Montgomery reduction paths require that arithmetic on q stays below
     * the 2^64 boundary, so states above this bound are unsupported.
     */
    if (q > (UINT64_MAX >> 1)) {
        NTT_LOG(NTT_LOG_ERROR,
                "Invalid modulus q=%llu: scalar adapter supports q <= 2^63",
                (unsigned long long)q);
        return false;
    }

    /*
     * The scalar backend requires a prime modulus over which the radix-2
     * transforms are defined. ntt_is_prime() rejects values below 2 and
     * composite values.
     */
    if (ntt_is_prime(q) == false) {
        NTT_LOG(NTT_LOG_ERROR,
                "Invalid modulus q=%llu: modulus is not prime",
                (unsigned long long)q);
        return false;
    }

    /*
     * Montgomery REDC requires gcd(q, R) = 1 so that q is invertible modulo
     * the radix R = 2^32|2^64. This holds for every odd q and in particular
     * rejects the only even prime, q = 2. Barrett has no such restriction.
     */
    if ((flags & NTT_CONFIG_REDUCTION_MONTGOMERY) && q == 2u) {
        NTT_LOG(NTT_LOG_ERROR,
                "Invalid modulus q=%llu: Montgomery REDC requires "
                "an odd modulus",
                (unsigned long long)q);
        return false;
    }

    /*
     * Barrett requires nothing beyond primality and the 2^63 bound already
     * enforced above. For q < 2^32 the reciprocal mu = floor(2^64/q) is
     * computed with ntt_scalar_barrett_mu(), while larger moduli use the
     * two-word mu128 formulation; both hold for any supported q, so no
     * additional validation is needed here.
     */

    return true;
}

/**
 * @brief Initializes the scalar NTT adapter state.
 *
 * Allocates and initializes the backend-specific state required by the scalar
 * NTT implementation. The setup phase prepares all arithmetic constants and
 * precomputed tables required by the forward NTT, inverse NTT, and negacyclic
 * polynomial multiplication routines.
 *
 * The scalar adapter supports two mutually exclusive modular reduction
 * backends:
 *
 * - Barrett reduction, using a reciprocal stored in @p state->barrett_mu
 *   (single-word, q < 2^32) or @p state->barrett_mu_hi/@p barrett_mu_lo
 *   (two-word, q <= 2^63 - 1).
 * - Montgomery reduction, using the Montgomery constants @p state->mont_r2
 *   and @p state->mont_qinv, with radix R = 2^32 (q < 2^32) or R = 2^64
 *   (q <= 2^63).
 *
 * Both backends support the full q <= 2^63 - 1 range. Barrett keeps values in
 * canonical representation. Montgomery uses an xR mod q representation.
 *
 * @param[in] config NTT configuration containing the modulus, transform size,
 *                   primitive roots, and arithmetic reduction flags.
 *
 * @return Pointer to the initialized scalar adapter state on success.
 * @return NULL on failure.
 */
void *ntt__scalar_adapter_setup(const ntt_config *config)
{
    uint64_t q, omega, psi;
    uint32_t n, flags;
    if (config == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid scalar adapter configuration");
        return NULL;
    }

    q = ntt_config_get_modulus(config);
    n = ntt_config_get_size(config);
    omega = ntt_config_get_omega(config);
    psi = ntt_config_get_psi(config);
    flags = ntt_config_get_flags(config);

    /*
     * Barrett and Montgomery reduction define different internal arithmetic
     * representations and therefore cannot be used simultaneously. The
     * absence of the Montgomery flag selects Barrett reduction as the default
     * backend.
     */
    if ((flags & NTT_CONFIG_REDUCTION_BARRETT) &&
        (flags & NTT_CONFIG_REDUCTION_MONTGOMERY)) {
        NTT_LOG(NTT_LOG_ERROR,
                "Invalid reduction flags: Barrett and Montgomery "
                "reduction cannot be enabled simultaneously");
        return NULL;
    }

    ntt_scalar_state *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Scalar state allocation failed");
        return NULL;
    }

    /*
     * Record the modulus, transform size, and whether the modulus fits in a
     * single 32-bit word. The q32 flag selects the single-word reduction path
     * (radix R = 2^32) versus the two-word path (radix R = 2^64) for both the
     * Barrett and Montgomery backends.
     */
    state->q = q;
    state->n = n;
    state->q32 = (q <= NTT_SCALAR_Q32_MAX);

    /*
     * Select the arithmetic backend once during setup. All subsequent
     * arithmetic operations are dispatched through the generic scalar
     * arithmetic wrappers, which use this field to select either Barrett
     * or Montgomery reduction.
     */
    state->reduction = (flags & NTT_CONFIG_REDUCTION_MONTGOMERY)
                           ? NTT_SCALAR_REDUCTION_MONTGOMERY
                           : NTT_SCALAR_REDUCTION_BARRETT;

    /*
     * Initialize the reduction-specific constants before converting any
     * roots into the internal representation.
     *
     * Barrett reduction requires the precomputed reciprocal of the modulus,
     * mu = floor(2^64 / q), using a single-word mu for q < 2^32 and a
     * two-word (mu_hi, mu_lo) pair for q <= 2^63 - 1.
     *
     * Montgomery reduction requires R^2 mod q and the negative modular
     * inverse of q modulo R, where R = 2^32 for q32 and R = 2^64 otherwise.
     *
     * These constants are used by the generic scalar arithmetic wrappers and
     * remain unchanged for the lifetime of the adapter state.
     */
    if (state->reduction == NTT_SCALAR_REDUCTION_BARRETT) {
        if (state->q32) {
            state->barrett_mu = ntt_scalar_barrett_mu((uint32_t)q);
        } else {
            ntt_scalar_barrett_mu128(q,
                                     &state->barrett_mu_hi,
                                     &state->barrett_mu_lo);
        }
    } else {
        state->mont_r2 = ntt_scalar_mont_r2(q, state->q32);
        state->mont_qinv = ntt_scalar_mont_qinv(q, state->q32);
    }

    /*
     * Canonicalize the roots supplied by the common NTT context layer.
     *
     * omega is the primitive n-th root of unity used by the cyclic NTT.
     * psi is the primitive 2n-th root used for the negacyclic twist.
     *
     * The roots are converted into the representation expected by the
     * selected arithmetic backend. For Barrett this is the canonical residue
     * modulo q. For Montgomery this is the corresponding Montgomery-domain
     * representation.
     */
    state->omega = ntt__scalar_canonicalize_value(omega, state);
    state->psi = ntt__scalar_canonicalize_value(psi, state);

    /*
     * Compute the inverse roots required by the inverse transform and the
     * inverse negacyclic twist. The modular inverse is computed after
     * canonicalization so that the inversion routine operates on a valid
     * internal representation.
     */
    state->omega_inv = scalar_modinv(state->omega, state);
    state->psi_inv = scalar_modinv(state->psi, state);

    /*
     * Inverse of the transform length used to normalize the inverse NTT.
     * Computed canonically once during setup.
     */
    uint64_t n_reduced = ntt__scalar_canonicalize_value(n, state);
    state->n_inv = ntt__scalar_modpow(n_reduced, q - 2, state);

    /*
     * A radix-2 NTT of size n has log2(n) butterfly stages.
     *
     * Each iteration divides the current transform length by two:
     *
     *     n, n/2, n/4, ..., 2
     *
     * Therefore, the number of iterations is exactly log2(n). The transform
     * size is expected to be a power of two because the scalar backend uses
     * radix-2 Cooley-Tukey and Gentleman-Sande algorithms.
     */
    for (uint32_t t = n; t > 1; t >>= 1) {
        state->stages++;
    }

    /*
     * Allocate one stage root for each radix-2 butterfly stage.
     *
     * fwd_twiddle[stage] stores the root used by the corresponding
     * Cooley-Tukey forward stage.
     *
     * inv_twiddle[stage] stores the inverse root used by the corresponding
     * Gentleman-Sande inverse stage.
     *
     * The arrays contain only one root per stage because the individual
     * butterfly twiddles within a stage are generated iteratively by
     * multiplying the current twiddle by the stage root.
     */
    state->fwd_twiddle = calloc(state->stages, sizeof(uint64_t));
    state->inv_twiddle = calloc(state->stages, sizeof(uint64_t));

    /*
     * Allocate the powers of psi and psi^{-1} used by the negacyclic
     * twisting and inverse untwisting operations.
     *
     * One entry is required for each polynomial coefficient:
     *
     *     psi_pow[i]     = psi^i
     *     psi_inv_pow[i] = psi^{-i}.
     */
    state->psi_pow = calloc(n, sizeof(uint64_t));
    state->psi_inv_pow = calloc(n, sizeof(uint64_t));

    /*
     * Allocate a lookup table containing the bit-reversed index of every
     * position in an n-point transform.
     */
    state->bitrev = calloc(n, sizeof(uint32_t));

    if (state->fwd_twiddle == NULL || state->inv_twiddle == NULL ||
        state->psi_pow == NULL || state->psi_inv_pow == NULL ||
        state->bitrev == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Scalar adapter table allocation failed");
        goto cleanup;
    }

    /*
     * Precompute the bit-reversal permutation used by the iterative radix-2
     * transforms. Caching these indices avoids recomputing the permutation
     * for every forward and inverse NTT, replacing repeated bit manipulations
     * with simple table lookups throughout the lifetime of the context.
     */
    for (uint32_t i = 0; i < n; i++) {
        state->bitrev[i] = ntt_reverse_bits(i, state->stages);
    }

    /*
     * Precompute the principal stage roots for the radix-2 transforms.
     *
     * At a stage with butterfly size m, butterflies operate on blocks of m
     * coefficients. The root that advances the twiddle factor between
     * consecutive butterflies is
     *
     *     omega^(n / m).
     *
     * This is a primitive m-th root of unity because omega has order n:
     *
     *     (omega^(n/m))^m = omega^n = 1.
     *
     * As m doubles at each stage, the required root order also doubles:
     *
     *     m = 2, 4, 8, ..., n.
     *
     * The inverse transform uses the corresponding inverse root derived from
     * omega_inv.
     *
     * The roots are computed once during setup rather than with modular
     * exponentiation inside every transform. They are then encoded into the
     * selected arithmetic representation so that the transform hot path can
     * use them directly without performing representation conversions.
     */
    for (uint32_t stage = 0, m = 2; stage < state->stages; stage++, m <<= 1) {
        uint64_t fw = scalar_root_power(state->omega, n / m, state);
        uint64_t iw = scalar_root_power(state->omega_inv, n / m, state);

        state->fwd_twiddle[stage] = ntt__scalar_encode_value(fw, state);
        state->inv_twiddle[stage] = ntt__scalar_encode_value(iw, state);
    }

    /*
     * Initialize the zeroth powers:
     *
     *     psi^0    = 1
     *     psi^{-0} = 1.
     *
     * These values are encoded into the selected arithmetic representation
     * because all subsequent power generation uses the generic scalar
     * multiplication wrapper.
     */
    state->psi_pow[0] = ntt__scalar_encode_value(1, state);
    state->psi_inv_pow[0] = ntt__scalar_encode_value(1, state);

    /*
     * Encode psi and psi^{-1} into the selected internal representation.
     *
     * This is required before the iterative power generation below. In
     * Montgomery mode, for example, multiplying a Montgomery-domain value
     * by a canonical-domain value would produce an incorrectly represented
     * result.
     */
    uint64_t psi_r = ntt__scalar_encode_value(state->psi, state);
    uint64_t psi_inv_r = ntt__scalar_encode_value(state->psi_inv, state);

    /*
     * Generate all powers iteratively:
     *
     *     psi^i     = psi^(i-1)    * psi
     *     psi^(-i)  = psi^(-(i-1)) * psi^(-1).
     *
     * This requires only one modular multiplication per table entry and
     * avoids performing a modular exponentiation independently for every
     * coefficient. The generic scalar multiplication wrapper automatically
     * applies the selected Barrett or Montgomery reduction.
     */
    for (uint32_t i = 1; i < n; i++) {
        state->psi_pow[i] =
            ntt__scalar_mul(state->psi_pow[i - 1], psi_r, state);
        state->psi_inv_pow[i] =
            ntt__scalar_mul(state->psi_inv_pow[i - 1], psi_inv_r, state);
    }

    return state;

cleanup:
    ntt__scalar_adapter_teardown(state);
    return NULL;
}

/**
 * @brief Releases all resources owned by a scalar NTT adapter state.
 *
 * Frees the precomputed twiddle, twist, and bit-reversal tables, clears the
 * state structure, and finally releases the state allocation. Passing NULL
 * is safe and has no effect.
 *
 * @param state_ptr Scalar adapter state returned by @ref
 * ntt__scalar_adapter_setup.
 */
void ntt__scalar_adapter_teardown(void *state_ptr)
{
    ntt_scalar_state *s = state_ptr;
    if (s == NULL) {
        return;
    }

    SAFE_FREE(s->fwd_twiddle);
    SAFE_FREE(s->inv_twiddle);
    SAFE_FREE(s->psi_pow);
    SAFE_FREE(s->psi_inv_pow);
    ZERO_STRUCTP(s);
    SAFE_FREE(s);
}

/**
 * @brief Executes the internal radix-2 Cooley-Tukey forward NTT.
 *
 * Performs an in-place decimation-in-time (DIT) Number Theoretic Transform
 * using the iterative radix-2 Cooley-Tukey algorithm. The input is first
 * permuted into bit-reversed order via the cached lookup table, after which
 * log2(n) butterfly stages are executed.
 *
 * Each butterfly computes
 * @f[
 * (u,v) \mapsto (u + v,\; u - v),
 * @f]
 * where the second input is multiplied by the current twiddle factor before
 * the butterfly operation. Twiddle factors are generated from the precomputed
 * stage roots stored in the scalar adapter state.
 *
 * All arithmetic operations are dispatched through the selected reduction
 * backend.
 *   - In Barrett mode, coefficients remain in canonical modular
 *     representation.
 *   - In Montgomery mode, coefficients remain in Montgomery representation
 *     throughout the transform.
 *
 * This function operates exclusively on the internal arithmetic
 * representation selected by the scalar backend. No conversion between
 * canonical and backend-specific representations is performed here.
 * Representation conversion is handled at the transform API boundaries.
 *
 * @param[in] state Scalar adapter state containing the transform parameters,
 *                  precomputed stage roots, and selected reduction backend.
 * @param[in] a     Array of @p s->n coefficients in the selected internal
 *                  domain.
 *
 * @return NTT_OK on success
 * @return NTT_ERROR for invalid arguments.
 */
static int scalar_forward_internal(ntt_scalar_state *state, uint64_t *a)
{
    int rc;

    if (state == NULL || a == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }

    rc = scalar_bitrev_permute_cached(a, state->bitrev, state->n);
    if (rc != NTT_OK) {
        NTT_LOG(NTT_LOG_ERROR, "Bit reversal failed");
        return NTT_ERROR;
    }

    for (uint32_t stage = 0, m = 2; stage < state->stages; stage++, m <<= 1) {
        uint32_t half = m >> 1;
        uint64_t wm = state->fwd_twiddle[stage];

        for (uint32_t k = 0; k < state->n; k += m) {
            uint64_t w = ntt__scalar_encode_value(1, state);

            for (uint32_t j = 0; j < half; j++) {
                uint64_t u = a[k + j];
                uint64_t v = ntt__scalar_mul(a[k + j + half], w, state);

                a[k + j] = ntt__scalar_add(u, v, state);
                a[k + j + half] = ntt__scalar_sub(u, v, state);
                w = ntt__scalar_mul(w, wm, state);
            }
        }
    }

    return NTT_OK;
}

/**
 * @brief Executes the internal radix-2 Gentleman-Sande inverse NTT.
 *
 * Performs an in-place decimation-in-frequency (DIF) inverse Number
 * Theoretic Transform using the iterative radix-2 Gentleman-Sande
 * algorithm. The transform processes butterfly stages with lengths
 * decreasing from @p n to two. Each butterfly computes
 * @f[
 * (u,v) \mapsto (u+v,\;(u-v)w),
 * @f]
 * where @p w is the appropriate inverse twiddle factor.
 *
 * After the final butterfly stage, the output is in bit-reversed order
 * and is therefore permuted back into natural order using the cached lookup
 * table. The result is then multiplied by @f$n^{-1} \bmod q@f$ to obtain the
 * correctly normalized inverse NTT.
 *
 * All arithmetic operations are dispatched through the selected reduction
 * backend. In Barrett mode, coefficients remain in canonical modular
 * representation. In Montgomery mode, coefficients remain in Montgomery
 * representation throughout the transform.
 *
 * This function operates exclusively on the internal arithmetic
 * representation selected by the scalar backend. No conversion between
 * canonical and backend-specific representations is performed here.
 * Representation conversion is handled at the transform API boundaries.
 *
 * @param[in] state Scalar adapter state containing the transform parameters,
 *                  precomputed inverse stage roots, inverse transform scale,
 *                  and selected reduction backend.
 * @param[in,out] a Array of @p s->n coefficients in the backend's internal
 *                  representation. On success, the array contains the
 *                  inverse NTT in the same representation and in natural
 *                  coefficient order.
 *
 * @return NTT_OK    The inverse NTT completed successfully.
 * @return NTT_ERROR @p s or @p a is NULL, or the bit-reversal permutation
 *                   failed.
 *
 * @see scalar_forward_internal()
 */
static int scalar_inverse_internal(ntt_scalar_state *state, uint64_t *a)
{
    int rc;

    if (state == NULL || a == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }

    for (uint32_t stage = 0, m = state->n; stage < state->stages;
         stage++, m >>= 1) {
        uint32_t half = m >> 1;
        uint64_t wm = state->inv_twiddle[state->stages - 1 - stage];

        for (uint32_t k = 0; k < state->n; k += m) {
            uint64_t w = ntt__scalar_encode_value(1, state);

            for (uint32_t j = 0; j < half; j++) {
                uint64_t u = a[k + j];
                uint64_t v = a[k + j + half];

                a[k + j] = ntt__scalar_add(u, v, state);
                a[k + j + half] =
                    ntt__scalar_mul(ntt__scalar_sub(u, v, state), w, state);
                w = ntt__scalar_mul(w, wm, state);
            }
        }
    }

    rc = scalar_bitrev_permute_cached(a, state->bitrev, state->n);
    if (rc != NTT_OK) {
        NTT_LOG(NTT_LOG_ERROR, "Bit reversal failed");
        return NTT_ERROR;
    }

    uint64_t n_inv = ntt__scalar_encode_value(state->n_inv, state);
    for (uint32_t i = 0; i < state->n; i++) {
        a[i] = ntt__scalar_mul(a[i], n_inv, state);
    }

    return NTT_OK;
}

/**
 * @brief Computes the public forward NTT using Cooley-Tukey DIT.
 *
 * Converts all input coefficients into the selected backend representation,
 * executes the internal radix-2 Cooley-Tukey transform, and decodes the
 * output back to canonical @f$[0,q)@f$ representation. Montgomery mode
 * therefore performs only one encode pass before the transform and one decode
 * pass after it.
 *
 * @note No Montgomery conversion occurs inside the butterfly loops.
 *
 * @param state_ptr Scalar adapter state returned by
 *                  @ref ntt__scalar_adapter_setup.
 * @param a         In/out coefficient array of length @p n. Input and output
 *                  values are always in canonical representation at the public
 *                  API boundary.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR if the state or array is NULL.
 */
int ntt__scalar_forward(void *state_ptr, uint64_t *a)
{
    ntt_scalar_state *state = state_ptr;
    int rc;
    if (state == NULL || a == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }

    for (uint32_t i = 0; i < state->n; i++) {
        a[i] = ntt__scalar_encode_value(a[i], state);
    }

    rc = scalar_forward_internal(state, a);
    if (rc != NTT_OK) {
        return rc;
    }

    for (uint32_t i = 0; i < state->n; i++) {
        a[i] = ntt__scalar_decode_value(a[i], state);
    }

    return NTT_OK;
}

/**
 * @brief Computes the public inverse NTT using Gentleman-Sande DIF.
 *
 * Encodes the canonical input into the selected backend representation,
 * executes the internal Gentleman-Sande inverse transform, applies the
 * inverse-length scaling factor, and decodes the result back to canonical
 * representation.
 *
 * @param state_ptr Scalar adapter state returned by
 *                  @ref ntt__scalar_adapter_setup.
 * @param a         In/out coefficient array of length @p n. Input and output
 *                  values are always in canonical representation at the public
 *                  API boundary.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR if the state or array is NULL.
 */
int ntt__scalar_inverse(void *state_ptr, uint64_t *a)
{
    ntt_scalar_state *state = state_ptr;
    int rc;

    if (state == NULL || a == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }

    for (uint32_t i = 0; i < state->n; i++) {
        a[i] = ntt__scalar_encode_value(a[i], state);
    }

    rc = scalar_inverse_internal(state, a);
    if (rc != NTT_OK) {
        return rc;
    }

    for (uint32_t i = 0; i < state->n; i++) {
        a[i] = ntt__scalar_decode_value(a[i], state);
    }

    return NTT_OK;
}

/**
 * @brief Multiplies two polynomials using the negacyclic Number Theoretic
 * Transform (NTT).
 *
 * Computes the product
 * @f[
 * c(x) = a(x)b(x) mod (x^n + 1, q),
 * @f]
 * where q is the modulus and n is the transform size stored in the NTT
 * context.
 *
 * The multiplication is performed using the standard "twisting" technique to
 * convert a negacyclic convolution into a cyclic convolution:
 *   1. Multiply each coefficient by psi^i.
 *   2. Compute the Cooley-Tukey (CT) forward NTT of both operands.
 *   3. Perform pointwise multiplication in the transform domain.
 *   4. Compute the Gentleman-Sande (GS) inverse NTT.
 *   5. Multiply each coefficient by psi^{-i} to recover the negacyclic
 *      product.
 *
 * Temporary buffers are allocated internally and freed before the function
 * returns.
 *
 * @param[in] state_ptr Scalar adapter state.
 * @param[in] a  First input polynomial with @p n coefficients.
 * @param[in] b  Second input polynomial with @p n coefficients.
 * @param[out] c Output polynomial with @p n coefficients. It may alias neither
 *               input according to the adapter's current temporary-buffer
 *               strategy.
 *
 * @return NTT_OK Multiplication completed successfully.
 * @return NTT_ERROR on errors.
 */
int ntt__scalar_negacyclic_mul(void *state_ptr,
                               uint64_t *a,
                               uint64_t *b,
                               uint64_t *c)
{
    if (state_ptr == NULL || a == NULL || b == NULL || c == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }

    ntt_scalar_state *state = state_ptr;
    uint32_t n = state->n;
    int rc;

    /* Twisted copies of a and b */
    uint64_t *ta = calloc(n, sizeof(uint64_t));
    uint64_t *tb = calloc(n, sizeof(uint64_t));
    if (ta == NULL || tb == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Error while allocating twisted a and b");
        rc = NTT_ERROR;
        goto cleanup;
    }

    /* Step 1: twist by psi^i */
    for (uint32_t i = 0; i < n; i++) {
        ta[i] = ntt__scalar_mul(ntt__scalar_encode_value(a[i], state),
                                state->psi_pow[i],
                                state);
        tb[i] = ntt__scalar_mul(ntt__scalar_encode_value(b[i], state),
                                state->psi_pow[i],
                                state);
    }

    /* Step 2: forward NTT (cyclic) on each */
    rc = scalar_forward_internal(state, ta);
    if (rc != NTT_OK) {
        goto cleanup;
    }
    rc = scalar_forward_internal(state, tb);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    /* Step 3: pointwise multiply, reuse ta as the output buffer */
    for (uint32_t i = 0; i < n; i++) {
        ta[i] = ntt__scalar_mul(ta[i], tb[i], state);
    }

    /* Step 4: inverse NTT */
    rc = scalar_inverse_internal(state, ta);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    /* Step 5: untwist by psi^{-i} to undo step 1 */
    for (uint32_t i = 0; i < n; i++) {
        uint64_t x = ntt__scalar_mul(ta[i], state->psi_inv_pow[i], state);
        c[i] = ntt__scalar_decode_value(x, state);
    }

cleanup:
    SAFE_FREE(ta);
    SAFE_FREE(tb);
    return rc;
}
