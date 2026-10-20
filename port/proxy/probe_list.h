
#pragma once

#include <cstdint>
#include <cstring>

namespace probe {

constexpr unsigned kMaxTerms = 32;

struct Term {
    uint32_t address;
    uint32_t offset;
    bool indirect;
    bool has_index;
    uint32_t index_address;
    uint32_t index_offset;
    bool index_indirect;
    uint32_t field;
    char text[40];
};

inline const char* skip_space(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return p;
}

inline bool hex(const char** p, uint32_t* out) {
    const char* q = *p;
    if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) q += 2;
    uint64_t value = 0;
    unsigned digits = 0;
    for (;;) {
        unsigned char c = (unsigned char)*q;
        unsigned d = c >= '0' && c <= '9'     ? c - '0'
                     : c >= 'a' && c <= 'f'   ? c - 'a' + 10
                     : c >= 'A' && c <= 'F'   ? c - 'A' + 10
                                              : 16u;
        if (d == 16u) break;
        value = value * 16 + d;
        if (value > 0xffffffffull) return false;
        ++digits;
        ++q;
    }
    if (!digits) return false;
    *out = (uint32_t)value;
    *p = q;
    return true;
}

inline bool base(const char** p, uint32_t* address, uint32_t* offset, bool* indirect) {
    *offset = 0;
    *indirect = false;
    if (!hex(p, address)) return false;
    if (**p == '+') {
        ++*p;
        if (!hex(p, offset)) return false;
        *indirect = true;
    }
    return true;
}

inline bool parse(const char* text, Term* terms, unsigned capacity, unsigned* count) {
    *count = 0;
    const char* p = skip_space(text);
    if (!*p) return false;
    unsigned used = 0;
    for (;;) {
        if (used >= capacity || used >= kMaxTerms) return false;
        const char* start = p;
        Term term = {};
        if (!base(&p, &term.address, &term.offset, &term.indirect)) return false;
        if (*p == '@') {
            ++p;
            if (!base(&p, &term.index_address, &term.index_offset, &term.index_indirect))
                return false;
            if (*p != '.') return false;
            ++p;
            if (!hex(&p, &term.field)) return false;
            term.has_index = true;
        }
        unsigned len = (unsigned)(p - start);
        if (len >= sizeof term.text) return false;
        std::memcpy(term.text, start, len);
        term.text[len] = 0;
        terms[used++] = term;
        p = skip_space(p);
        if (!*p) break;
        if (*p != ',') return false;
        p = skip_space(p + 1);
    }
    *count = used;
    return true;
}

}
