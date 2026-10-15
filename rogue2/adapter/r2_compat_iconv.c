#include "compat/iconv.h"

#include <stdint.h>
#include <string.h>

iconv_t iconv_open(const char *to_encoding, const char *from_encoding)
{
    (void)to_encoding;
    (void)from_encoding;
    return (iconv_t)(uintptr_t)1;
}

size_t iconv(iconv_t cd, char **input, size_t *input_left, char **output, size_t *output_left)
{
    size_t count;
    (void)cd;
    if (input == NULL || input_left == NULL || output == NULL || output_left == NULL) {
        return 0;
    }
    count = (*input_left < *output_left) ? *input_left : *output_left;
    if (count > 0) {
        memcpy(*output, *input, count);
        *input += count;
        *output += count;
        *input_left -= count;
        *output_left -= count;
    }
    return (*input_left == 0) ? 0 : (size_t)-1;
}

int iconv_close(iconv_t cd)
{
    (void)cd;
    return 0;
}
