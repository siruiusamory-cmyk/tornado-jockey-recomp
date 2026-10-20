
#pragma once

#include <cstdio>

namespace backend {

inline bool translated(int faithful) { return faithful == 0; }

inline void path(int faithful, const char* module_dir, const char* system_dir, char* out,
                 size_t size) {
    if (translated(faithful)) {
        std::snprintf(out, size, "%s\\..\\build\\proxy\\d3d8to9.dll", module_dir);
    } else {
        std::snprintf(out, size, "%s\\d3d8.dll", system_dir);
    }
}

}
