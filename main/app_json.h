// Tiny JSON string extractor for Google REST replies (host-testable, no deps).
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Copies the first JSON string value for "key" into out (unescapes \\ \" \/ \n \t).
// Returns false if missing/truncated/overflow.
bool app_json_extract_string(const char *json, const char *key,
                             char *out, size_t out_len);
