#include <assert.h>
#include <string.h>
#include "app_json.h"

int main(void)
{
    char out[64];
    const char *stt =
        "{\"results\":[{\"alternatives\":[{\"transcript\":\"hello passport\",\"confidence\":0.9}]}]}";
    assert(app_json_extract_string(stt, "transcript", out, sizeof(out)));
    assert(strcmp(out, "hello passport") == 0);

    const char *tr =
        "{\"data\":{\"translations\":[{\"translatedText\":\"ni hao\",\"detectedSourceLanguage\":\"en\"}]}}";
    assert(app_json_extract_string(tr, "translatedText", out, sizeof(out)));
    assert(strcmp(out, "ni hao") == 0);

    assert(app_json_extract_string("{\"a\":\"x\\\"y\"}", "a", out, sizeof(out)));
    assert(strcmp(out, "x\"y") == 0);

    assert(!app_json_extract_string("{}", "missing", out, sizeof(out)));
    assert(!app_json_extract_string(NULL, "a", out, sizeof(out)));
    return 0;
}
