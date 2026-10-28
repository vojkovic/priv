#include "engines/engine.h"

struct engine engine_askubuntu = {
    .name = "askubuntu",
    .shortcut = "ubuntu",
    .website = "askubuntu.com",
    .bang = "https://askubuntu.com/search?q=%s",
    .category = "tech",
    .paging = 1,
    .search = stackexchange_search,
};
