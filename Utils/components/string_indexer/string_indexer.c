#include "string_indexer.h"
#include <string.h>
#include <ctype.h>

static const char *const palabras[] = {
    "Hola",
    "mundo",
    "C",
    "arreglos de cadenas",
    "¡funcionan!"
};
static const size_t palabras_count = sizeof(palabras) / sizeof(palabras[0]);

size_t si_count(void) { return palabras_count; }

static void trim_view(const char *s, const char **out_start, size_t *out_len) {
    if (!s) { *out_start = NULL; *out_len = 0; return; }
    const char *start = s; while (*start && isspace((unsigned char)*start)) start++;
    if (*start == '\0') { *out_start = start; *out_len = 0; return; }
    const char *end = start + strlen(start) - 1; while (end > start && isspace((unsigned char)*end)) end--;
    *out_start = start; *out_len = (size_t)(end - start + 1);
}

int si_find_index_trim(const char *entrada) {
    const char *tstart; size_t tlen; trim_view(entrada, &tstart, &tlen);
    if (tlen == 0) return -1;
    for (size_t i = 0; i < palabras_count; ++i) {
        const char *p = palabras[i]; size_t plen = strlen(p);
        if (plen == tlen && strncmp(p, tstart, tlen) == 0) return (int)i;
    }
    return -1;
}

static int strncasecmp_ascii(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        int da = tolower((unsigned char)a[i]); int db = tolower((unsigned char)b[i]);
        if (da != db) return (da < db) ? -1 : 1;
    }
    return 0;
}

int si_find_index_trim_ci(const char *entrada) {
    const char *tstart; size_t tlen; trim_view(entrada, &tstart, &tlen);
    if (tlen == 0) return -1;
    for (size_t i = 0; i < palabras_count; ++i) {
        const char *p = palabras[i]; size_t plen = strlen(p);
        if (plen == tlen && strncasecmp_ascii(p, tstart, tlen) == 0) return (int)i;
    }
    return -1;
}
