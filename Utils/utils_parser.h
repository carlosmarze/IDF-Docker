#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*on_pair_cb)(const char *key, const char *value, void *ctx);

int parse_pairs_ext(const char *buf,
                    const char *sep1,
                    const char *sep2,
                    on_pair_cb cb,
                    void *ctx);

#ifdef __cplusplus
}
#endif
