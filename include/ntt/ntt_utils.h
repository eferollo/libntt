/*
 * ntt_utils.h
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

#ifndef NTT_UTILS_H
#define NTT_UTILS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Public utility functions useful for NTT adapter implementations.
 *
 * This header provides generic utility functions that may be useful when
 * implementing custom NTT adapters, including adapters implemented outside
 * the library's internal source tree.
 *
 * The functions declared here are independent of any specific NTT arithmetic
 * backend and are intended to provide reusable building blocks for adapter
 * implementations. They form part of the public API and may be used by both
 * the library's built-in adapters and external adapter implementations.
 */

/**
 * @brief Tests whether a 32-bit unsigned integer is prime.
 *
 * Performs a deterministic Miller-Rabin primality test over the complete
 * uint32_t domain.
 *
 * The fixed witness set {2, 7, 61} is sufficient for every integer smaller
 * than 4,759,123,141. Since UINT32_MAX is 4,294,967,295, this bound covers
 * every possible uint32_t input. Therefore, unlike a general probabilistic
 * Miller-Rabin implementation, this function returns a mathematically
 * certain result for the complete supported input range.
 *
 * The implementation first handles small values and even integers directly.
 * For the remaining odd candidate, q - 1 is decomposed as
 *
 * @f[
 * q - 1 = d \cdot 2^s,
 * @f]
 *
 * where @f$d@f$ is odd. Each fixed witness is then tested using the standard
 * Miller-Rabin strong probable-prime test.
 *
 * The function is generic number-theory functionality and is therefore kept
 * independent of any specific arithmetic backend. The modulo operations used
 * here are acceptable because primality testing is performed only during
 * context initialization and is not part of the NTT hot path.
 *
 * @param[in] q Integer to test for primality.
 *
 * @return true if @p q is prime.
 * @return false if @p q is composite or less than two.
 */
bool ntt_is_prime(uint32_t q);

/**
 * @brief Checks whether a value is a power of two.
 *
 * @param[in] x Value to test.
 *
 * @return true If x is a power of two.
 * @return false Otherwise.
 */
bool ntt_is_power_of_two(uint32_t n);

/**
 * @brief Returns the bit-reversed representation of an integer.
 *
 * Reverses the least significant @p bits bits of @p x. For an N-point
 * radix-2 transform, @p bits is equal to @f$\log_2(N)@f$, so the result
 * corresponds to the bit-reversed index used by iterative NTT
 * algorithms.
 *
 * @param[in] x    Value whose bits are reversed.
 * @param[in] bits Number of least significant bits to reverse.
 *
 * @return Bit-reversed value of @p x over the specified bit width.
 */
uint32_t ntt_reverse_bits(uint32_t x, uint32_t bits);

#ifdef __cplusplus
}
#endif

#endif
