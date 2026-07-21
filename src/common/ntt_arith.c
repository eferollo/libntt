#include <stddef.h>
#include "ntt_internal.h"

/**
 * @brief Computes modular addition.
 *
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @param[in] q Modulus.
 *
 * @return (a + b) mod q.
 */
uint32_t ntt__addmod(uint32_t a, uint32_t b, uint32_t q)
{
    uint32_t s = a + b;
    return (s >= q) ? s - q : s;
}

/**
 * @brief Computes modular subtraction.
 *
 * @param[in] a Minuend.
 * @param[in] b Subtrahend.
 * @param[in] q Modulus.
 *
 * @return (a - b) mod q.
 */
uint32_t ntt__submod(uint32_t a, uint32_t b, uint32_t q)
{
    return (a >= b) ? (a - b) : (a + q - b);
}

/**
 * @brief Computes modular multiplication.
 *
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @param[in] q Modulus.
 *
 * @return (a * b) mod q.
 */
uint32_t ntt__mulmod(uint32_t a, uint32_t b, uint32_t q)
{
    return (uint32_t)(((uint64_t)a * (uint64_t)b) % q);
}

/**
 * @brief Computes modular exponentation using binary representation.
 *
 * Computes base^exp (mod q) using the square-and-multiply (binary
 * exponentation) algorithm. The exponent is processed one bit at a time from
 * LSB to MSB. For each set bit, the current result is multiplied by the
 * current power of the base. After each iteration, the base is squared and the
 * exponent is shifted right by one bit.
 *
 * The algorithm runs in O(log exp) modular multiplications.
 *
 * @param[in] base  Base of the exponentation.
 * @param[in] exp   Non-negative exponent.
 * @param[in] q     Modulus
 *
 * @return base^exp (mod q)
 */
uint32_t ntt__modpow(uint32_t base, uint32_t exp, uint32_t q)
{
    uint32_t result = 1 % q;
    base %= q;
    while (exp > 0) {
        if (exp & 1u) {
            result = ntt__mulmod(result, base, q);
        }
        base = ntt__mulmod(base, base, q);
        exp >>= 1;
    }
    return result;
}

/**
 * @brief Computes the modular inverse via Fermat's little theorem (q must be
 * prime).
 *
 * @param[in] a Integer whose modular inverse is to be computed.
 * @param[in] q Prime modulus.
 *
 * @return The multiplicative inverse of a modulo q.
 */
uint32_t ntt__modinv(uint32_t a, uint32_t q)
{
    return ntt__modpow(a, q - 2, q);
}

/**
 * @brief Checks whether a value is a power of two.
 *
 * @param[in] x Value to test.
 *
 * @return 1 If x is a power of two.
 * @return 0 Otherwise.
 */
int ntt__is_power_of_two(uint32_t x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

/**
 * @brief Reorders an array into bit-reversed order.
 *
 * Applies the standard in-place bit-reversal permutation used by iterative
 * radix-2 FFT and NTT algorithms. Each index is mapped to the integer whose
 * binary representation is the reverse of the original index, allowing the
 * subsequent butterfly stages to process contiguous elements.
 *
 * The permutation is performed in-place and runs in O(n) time using constant
 * additional memory.
 *
 * @param[in,out] a    Pointer to the array to permute.
 * @param[in] n        Number of elements in the array. Must be a power of two.
 *
 * @return 0 on success.
 * @return -1 if the input array pointer is NULL.
 */
int ntt__bitrev_permute(uint32_t *a, uint32_t n)
{
    if (a == NULL) {
        return -1;
    }

    for (uint32_t i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            uint32_t tmp = a[i];
            a[i] = a[j];
            a[j] = tmp;
        }
    }
    return 0;
}
