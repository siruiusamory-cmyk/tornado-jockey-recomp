
#pragma once

#include <cstdint>
#include <cstring>

namespace save {

constexpr char kMagic[8] = {'T', 'J', 'S', 'A', 'V', 'E', '0', '1'};
constexpr uint32_t kVersion = 1;
constexpr unsigned kHeaderBytes = 64;
constexpr unsigned kSectionEntryBytes = 12;
constexpr unsigned kMaxSections = 8;

enum Tag : uint32_t {
    kGlobals = 1,
    kClassMap = 2,
    kRoots = 3,
    kArray = 4,
    kActors = 5,
    kBlocks = 6,
    kSpawners = 7,
    kRand = 8,
};
constexpr unsigned kSectionCount = kRand;

struct Header {
    unsigned char magic[8];
    uint32_t version;
    uint32_t header_bytes;
    uint32_t section_count;
    uint32_t record_count;
    uint32_t class_count;
    uint32_t flags;
    unsigned char image_sha256[32];
};

struct Section {
    uint32_t tag;
    uint32_t offset;
    uint32_t bytes;
};

struct ClassEntry {
    uint32_t id;
    uint32_t vtable;
    uint32_t size;
};

struct Patch {
    uint32_t offset;
    uint32_t block;
    uint32_t at;
};

inline void put32(unsigned char*& p, uint32_t v) {
    std::memcpy(p, &v, 4);
    p += 4;
}

inline uint32_t get32(const unsigned char*& p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    p += 4;
    return v;
}

inline void write_header(unsigned char* out, const Header& h) {
    std::memcpy(out, &h, kHeaderBytes);
}

inline void write_directory(unsigned char* out, const Section* dir) {
    for (unsigned i = 0; i < kSectionCount; ++i) {
        unsigned char* p = out + i * kSectionEntryBytes;
        put32(p, dir[i].tag);
        put32(p, dir[i].offset);
        put32(p, dir[i].bytes);
    }
}

inline bool read_header(const unsigned char* data, unsigned size, Header* h) {
    if (size < kHeaderBytes) return false;
    std::memcpy(h, data, kHeaderBytes);
    if (std::memcmp(h->magic, kMagic, sizeof kMagic) != 0) return false;
    if (h->version != kVersion || h->header_bytes != kHeaderBytes) return false;
    if (h->section_count == 0 || h->section_count > kMaxSections) return false;
    return true;
}

inline bool section_valid(const Section& s, unsigned size) {
    return s.tag && s.bytes <= size && s.offset <= size && s.bytes <= size - s.offset;
}

struct RefTable {
    static constexpr unsigned kMaxBlocks = 8192;
    uint32_t id[kMaxBlocks];
    uint32_t address[kMaxBlocks];
    unsigned count;

    void clear() { count = 0; }

    bool add(uint32_t block, uint32_t at) {
        for (unsigned i = 0; i < count; ++i) {
            if (id[i] == block) return false;
        }
        if (count == kMaxBlocks) return false;
        id[count] = block;
        address[count] = at;
        ++count;
        return true;
    }

    bool find(uint32_t block, uint32_t* at) const {
        for (unsigned i = 0; i < count; ++i) {
            if (id[i] != block) continue;
            *at = address[i];
            return true;
        }
        return false;
    }
};

inline bool resolve_body(const RefTable& refs, unsigned char* body, unsigned size,
                         const Patch* patches, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        if (patches[i].offset > size || size - patches[i].offset < 4) return false;
        if (patches[i].block == 0) continue;
        uint32_t at = 0;
        if (!refs.find(patches[i].block, &at)) return false;
        const uint32_t value = at + patches[i].at;
        std::memcpy(body + patches[i].offset, &value, 4);
    }
    return true;
}

}
