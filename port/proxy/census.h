
#pragma once

#include <cstdint>
#include <cstring>

namespace census {

constexpr uint32_t kFactoryTable = 0x6D13E0;
constexpr uint32_t kAllocator = 0x45446B;
constexpr uint32_t kRdataLow = 0x656000;
constexpr uint32_t kRdataHigh = 0x6B2D00;
constexpr unsigned kMaxClasses = 64;
constexpr unsigned kFactoryBytes = 64;
constexpr unsigned kCtorBytes = 96;

struct Class {
    uint32_t id;
    uint32_t factory;
    uint32_t size;
    uint32_t vtable;
};

using ReadDword = bool (*)(uint32_t address, uint32_t* out);
using ReadBytes = bool (*)(uint32_t address, void* out, unsigned size);

inline bool alloc_size(const unsigned char* code, unsigned len, uint32_t base, uint32_t* size,
                       unsigned* end) {
    for (unsigned i = 0; i + 10 <= len; ++i) {
        if (code[i] != 0x68 || code[i + 5] != 0xE8) continue;
        int32_t rel = 0;
        std::memcpy(&rel, code + i + 6, 4);
        if (base + i + 10 + (uint32_t)rel != kAllocator) continue;
        std::memcpy(size, code + i + 1, 4);
        *end = i + 10;
        return true;
    }
    return false;
}

inline bool ctor_call(const unsigned char* code, unsigned len, uint32_t base, unsigned from,
                      uint32_t* ctor) {
    for (unsigned j = from; j + 5 <= len; ++j) {
        if (code[j] != 0xE8) continue;
        int32_t rel = 0;
        std::memcpy(&rel, code + j + 1, 4);
        *ctor = base + j + 5 + (uint32_t)rel;
        return true;
    }
    return false;
}

inline bool vtable_store(const unsigned char* code, unsigned len, uint32_t* vtable) {
    for (unsigned k = 0; k + 6 <= len; ++k) {
        if (code[k] != 0xC7) continue;
        unsigned imm = 0;
        if (code[k + 1] == 0x03 || code[k + 1] == 0x06 || code[k + 1] == 0x07) {
            imm = k + 2;
        } else if (code[k + 1] == 0x45) {
            imm = k + 3;
        } else {
            continue;
        }
        if (imm + 4 > len) continue;
        uint32_t value = 0;
        std::memcpy(&value, code + imm, 4);
        if (value < kRdataLow || value >= kRdataHigh) continue;
        *vtable = value;
        return true;
    }
    return false;
}

inline unsigned decode(ReadDword dword, ReadBytes bytes, Class* out, unsigned capacity) {
    unsigned n = 0;
    for (unsigned id = 0; id < kMaxClasses && n < capacity; ++id) {
        uint32_t factory = 0;
        if (!dword(kFactoryTable + 4 * id, &factory) || !factory) continue;
        unsigned char code[kFactoryBytes] = {};
        if (!bytes(factory, code, kFactoryBytes)) continue;
        uint32_t size = 0, ctor = 0, vtable = 0;
        unsigned end = 0;
        if (!alloc_size(code, kFactoryBytes, factory, &size, &end)) continue;
        unsigned char body[kCtorBytes] = {};
        if (ctor_call(code, kFactoryBytes, factory, end, &ctor) &&
            bytes(ctor, body, kCtorBytes)) {
            vtable_store(body, kCtorBytes, &vtable);
        }
        out[n].id = id;
        out[n].factory = factory;
        out[n].size = size;
        out[n].vtable = vtable;
        ++n;
    }
    return n;
}

inline const Class* by_vtable(const Class* list, unsigned count, uint32_t vtable) {
    if (!vtable) return nullptr;
    for (unsigned i = 0; i < count; ++i) {
        if (list[i].vtable == vtable) return &list[i];
    }
    return nullptr;
}

}
