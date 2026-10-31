#include "app_json.h"

#include <string.h>

bool app_json_extract_string(const char *json, const char *key,
                             char *out, size_t out_len)
{
    if (!json || !key || !out || out_len == 0) return false;
    out[0] = '\0';

    char needle[64];
    size_t key_len = strlen(key);
    if (key_len + 4 >= sizeof(needle)) return false;
    needle[0] = '"';
    memcpy(needle + 1, key, key_len);
    needle[1 + key_len] = '"';
    needle[2 + key_len] = '\0';

    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return false;
    p++;

    size_t o = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            if (!*p) return false;
            char e = *p++;
            switch (e) {
            case '"': case '\\': case '/': c = e; break;
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            default: c = e; break;
            }
        }
        if (o + 1 >= out_len) return false;
        out[o++] = c;
    }
    if (*p != '"') return false;
    out[o] = '\0';
    return true;
}
