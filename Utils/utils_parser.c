#include "utils_parser.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

static inline size_t match_token(const char *p, const char *tok) {
    if (!p || !tok || !*tok) return 0; size_t n = strlen(tok);
    return (strncmp(p, tok, n) == 0) ? n : 0;
}

static const char* skip_ws(const char *p) { while (p && *p && isspace((unsigned char)*p)) p++; return p; }
//static const char* skip_line_comment(const char *p) { if (p && p[0]=='/' && p[1]=='/') { while (*p && *p!='\n' && *p!='\r') p++; } return p; }
static const char* skip_line_comment(const char *p) { if (p && p[0]=='#') { while (*p && *p!='\n' && *p!='\r') p++; } return p; }
static const char* sanitize_pos(const char *p) {
    while (p && *p) {
        const char *q = skip_ws(p); if (q != p) { p = q; continue; }
        q = skip_line_comment(p); if (q != p) { p = q; continue; }
        break;
    }
    return p;
}
static void trim_inplace(char *s) {
    if (!s) return; size_t i=0; while (s[i] && isspace((unsigned char)s[i])) i++; if (i) memmove(s, s+i, strlen(s+i)+1);
    size_t len=strlen(s); while (len>0 && isspace((unsigned char)s[len-1])) s[--len]='\0';
}

static const char* read_quoted(const char *p, char *out, size_t out_sz) {
    size_t w=0; if (!p || *p!='"') { if(out && out_sz) out[0]='\0'; return p; } p++;
    while (*p) {
        if (*p=='\\') { if (*(p+1)=='"' || *(p+1)=='\\') { if (w+1<out_sz) out[w++]=*(p+1); p+=2; continue; } }
        if (*p=='"') { p++; break; }
        if (w+1<out_sz) out[w++]=*p; p++;
    }
    if (out_sz) out[w]='\0'; return p;
}

static const char* read_until_ext(const char *p, char *out, size_t out_sz,
                                  const char *stop_token, bool stop_on_ws, bool stop_on_comment)
{
    size_t w=0; while (p && *p) {
        if (stop_on_comment && p[0]=='#') break;
        size_t mt = match_token(p, stop_token); if (stop_token && mt>0) break;
        if (stop_on_ws && isspace((unsigned char)*p)) break;
        if (w+1<out_sz) out[w++]=*p; p++;
    }
    if (out_sz) out[w]='\0'; return p;
}

int parse_pairs_ext(const char *buf, const char *sep1, const char *sep2,  on_pair_cb cb, void *ctx) {
    if (!buf || !sep1 || !sep2 || !cb) return -2;
    if (strcmp(sep1, sep2) == 0) return -1;
    bool separator_is_space = (strlen(sep1) == 1 && sep1[0] == ' ');

    const char *p = buf; int count = 0;
    while (1) {
        p = sanitize_pos(p); if (!p || !*p) break;
        char key[128] = {0};
        if (*p == '"') p = read_quoted(p, key, sizeof(key));
        else p = read_until_ext(p, key, sizeof(key), sep2, true, true);
        trim_inplace(key);
        p = sanitize_pos(p); if (!p || !*p) break;
        size_t msep = match_token(p, sep2);
        //if (msep == 0) { while (*p && !match_token(p, sep1) && !(p[0]=='/' && p[1]=='/')) p++; p = sanitize_pos(p); continue; }
        if (msep == 0) { while (*p && !match_token(p, sep1) && !(p[0] == '#')) p++; p = sanitize_pos(p); continue; }
        p += msep;
        p = sanitize_pos(p); if (!p || !*p) break;
        char val[256] = {0};
        if (*p == '"') p = read_quoted(p, val, sizeof(val));
        else p = read_until_ext(p, val, sizeof(val), sep1, separator_is_space, true);
        trim_inplace(val);
        if (key[0] != '\0') { cb(key, val, ctx); count++; }
        size_t msep_pair = match_token(p, sep1); if (msep_pair > 0) p += msep_pair;
        p = sanitize_pos(p); if (!p || !*p) break;
    }
    return count;
}
