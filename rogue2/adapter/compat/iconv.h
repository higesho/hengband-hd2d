#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *iconv_t;

iconv_t iconv_open(const char *to_encoding, const char *from_encoding);
size_t iconv(iconv_t cd, char **input, size_t *input_left, char **output, size_t *output_left);
int iconv_close(iconv_t cd);

#ifdef __cplusplus
}
#endif
