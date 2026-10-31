#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void app_store_make_name(uint32_t id, char *name, size_t name_len);
bool app_store_parse_id(const char *name, uint32_t *id_out);
