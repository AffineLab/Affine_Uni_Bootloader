/* Small freestanding runtime avoids unavailable rv32imac newlib multilibs. */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--)
    {
        *d++ = (unsigned char)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--)
    {
        if (*x != *y)
        {
            return *x - *y;
        }
        ++x;
        ++y;
    }
    return 0;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s)
    {
        for (size_t i = 0; i < n; i++)
        {
            d[i] = s[i];
        }
    }
    else
    {
        while (n)
        {
            --n;
            d[n] = s[n];
        }
    }
    return dst;
}
