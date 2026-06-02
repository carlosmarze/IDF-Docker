#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int si_find_index_trim(const char *entrada);
int si_find_index_trim_ci(const char *entrada);
size_t si_count(void);
#ifdef __cplusplus
}
#endif
