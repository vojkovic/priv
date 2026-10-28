#include "engines/engine.h"

struct engine engine_stackoverflow = {
    .name = "stackoverflow",
    .shortcut = "st",
    .website = "stackoverflow.com",
    .bang = "https://stackoverflow.com/search?q=%s",
    .category = "tech",
    .paging = 1,
    .search = stackexchange_search,
};
