#include "engines/engine.h"

struct engine engine_superuser = {
    .name = "superuser",
    .shortcut = "su",
    .website = "superuser.com",
    .bang = "https://superuser.com/search?q=%s",
    .category = "tech",
    .paging = 1,
    .search = stackexchange_search,
};
