#include <stddef.h>

void *memset(void *destination, int value, size_t bytes)
{
    unsigned char *output = destination;

    for (size_t index = 0; index < bytes; ++index)
        output[index] = (unsigned char)value;
    return destination;
}

void *memcpy(void *restrict destination, const void *restrict source, size_t bytes)
{
    unsigned char *output = destination;
    const unsigned char *input = source;

    for (size_t index = 0; index < bytes; ++index)
        output[index] = input[index];
    return destination;
}
