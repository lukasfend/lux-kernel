/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Software implementation of 64-bit unsigned division helpers for 32-bit targets.
 */
#include <stdint.h>

/**
 * Compute the unsigned 64-bit quotient of numerator divided by denominator and
 * optionally store the remainder.
 *
 * @param numerator The dividend.
 * @param denominator The divisor.
 * @param remainder If non-null, receives the remainder after division; set to 0 when denominator is 0.
 * @returns The quotient (numerator / denominator). Returns 0 when denominator is 0.
 */
unsigned long long __udivmoddi4(unsigned long long numerator,
                                unsigned long long denominator,
                                unsigned long long *remainder)
{
    if (denominator == 0) {
        if (remainder) {
            *remainder = 0;
        }
        return 0;
    }

    unsigned long long quotient = 0;
    unsigned long long rem = 0;

    for (int bit = 63; bit >= 0; --bit) {
        rem = (rem << 1) | ((numerator >> bit) & 1ULL);
        if (rem >= denominator) {
            rem -= denominator;
            quotient |= (1ULL << bit);
        }
    }

    if (remainder) {
        *remainder = rem;
    }

    return quotient;
}