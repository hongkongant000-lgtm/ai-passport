#include <assert.h>
#include <string.h>
#include "app_store_names.h"

int main(void)
{
    char name[24];
    app_store_make_name(3, name, sizeof(name));
    assert(strcmp(name, "t0003.txt") == 0);

    uint32_t id = 0;
    assert(app_store_parse_id("t0042.txt", &id));
    assert(id == 42);
    assert(!app_store_parse_id("notes.txt", &id));
    assert(!app_store_parse_id("t12", &id));
    assert(!app_store_parse_id("t99999.bin", &id));
    return 0;
}
