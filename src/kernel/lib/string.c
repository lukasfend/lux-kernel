#include <string.h>

void *memset(void *dst, int value, size_t len)
{
    unsigned char *ptr = dst;
    while (len--) {
        *ptr++ = (unsigned char)value;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t len)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (len--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t len)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d == s || len == 0) {
        return dst;
    }

    if (d < s) {
        while (len--) {
            *d++ = *s++;
        }
    } else {
        d += len;
        s += len;
        while (len--) {
            *--d = *--s;
        }
    }

    return dst;
}

int memcmp(const void *lhs, const void *rhs, size_t len)
{
    const unsigned char *a = lhs;
    const unsigned char *b = rhs;

    while (len--) {
        unsigned char va = *a++;
        unsigned char vb = *b++;
        if (va != vb) {
            return (int)va - (int)vb;
        }
    }
    return 0;
}

int strcmp(const char *lhs, const char *rhs)
{
    while (*lhs && (*lhs == *rhs)) {
        ++lhs;
        ++rhs;
    }
    return (unsigned char)*lhs - (unsigned char)*rhs;
}

size_t strlen(const char *str)
{
    const char *s = str;
    while (*s) {
        ++s;
    }
    return (size_t)(s - str);
}
