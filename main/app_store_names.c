#include "app_store_names.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void app_store_make_name(uint32_t id, char *name, size_t name_len)
{
    if (!name || name_len == 0) return;
    snprintf(name, name_len, "t%04lu.txt", (unsigned long)(id % 10000u));
}

bool app_store_parse_id(const char *name, uint32_t *id_out)
{
    if (!name || name[0] != 't') return false;
    char *end = NULL;
    unsigned long v = strtoul(name + 1, &end, 10);
    if (!end || strcmp(end, ".txt") != 0) return false;
    if (id_out) *id_out = (uint32_t)v;
    return true;
}
