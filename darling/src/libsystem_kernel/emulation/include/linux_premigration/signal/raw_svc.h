#ifndef DARLING_RAW_SVC_H
#define DARLING_RAW_SVC_H
#include <stdbool.h>

/* Mach trap numbers are negative. Unsigned subtraction also handles INT_MIN,
 * which cannot be negated in its signed type. The real table size is passed
 * by its owner rather than duplicated here. */
static inline bool darling_svc_mach_index(int number, unsigned int count, unsigned int *index)
{
    if (number >= 0) return false;
    unsigned int magnitude = 0u - (unsigned int)number;
    if (magnitude >= count) return false;
    *index = magnitude;
    return true;
}
#endif
