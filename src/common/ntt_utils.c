#include "ntt_internal.h"
#include <stddef.h>

/**
 * @brief Checks whether a value is a power of two.
 *
 * @param[in] x Value to test.
 *
 * @return true If x is a power of two.
 * @return false Otherwise.
 */
bool ntt__is_power_of_two(uint32_t x)
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
