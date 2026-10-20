
#pragma once

#include <cstdint>

#include "probe_list.h"

namespace watch {

inline uint32_t dr7(unsigned count) {
    uint32_t value = 0x100;
    for (unsigned i = 0; i < count; ++i) {
        value |= 1u << (2 * i);
        value |= 0x1u << (16 + 4 * i);
        value |= 0x3u << (18 + 4 * i);
    }
    return value;
}

inline bool parse(const char* text, probe::Term* term) {
    probe::Term terms[2];
    unsigned count = 0;
    if (!probe::parse(text, terms, 2, &count) || count != 1) return false;
    *term = terms[0];
    return true;
}

}
