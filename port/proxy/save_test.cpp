
#include "save.h"

#include <cstdio>
#include <cstring>

namespace {

unsigned g_failures;

void check(bool ok, const char* what, int line) {
    if (ok) return;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++g_failures;
}

#define CHECK(cond) check((cond), #cond, __LINE__)

constexpr uint32_t kSaverBlock1 = 0x00170000;
constexpr uint32_t kSaverBlock2 = 0x00180000;
constexpr uint32_t kLoaderBlock1 = 0x0F100000;
constexpr uint32_t kLoaderBlock2 = 0x0F200000;
constexpr uint32_t kRecordId = 7;

void store(unsigned char* p, uint32_t v) { std::memcpy(p, &v, 4); }

unsigned build_file(unsigned char* out) {
    save::Header h = {};
    std::memcpy(h.magic, save::kMagic, sizeof h.magic);
    h.version = save::kVersion;
    h.header_bytes = save::kHeaderBytes;
    h.section_count = save::kSectionCount;
    h.record_count = 1;
    h.class_count = 1;
    h.flags = 0;
    unsigned char* p = out;
    save::write_header(p, h);
    p += save::kHeaderBytes;

    save::Section dir[save::kSectionCount] = {};
    dir[save::kClassMap] = {save::kClassMap, 0, 12};
    dir[save::kBlocks] = {save::kBlocks, 0, 2 * 8 + 2 * 8};
    dir[save::kActors] = {save::kActors, 0, 24 + 2 * 12 + 16};
    save::write_directory(p, dir);
    p += save::kSectionCount * 12;

    save::put32(p, 9);
    save::put32(p, 0x00658EF8);
    save::put32(p, 0x2C58);

    for (unsigned i = 0; i < 2; ++i) {
        save::put32(p, i + 1);
        save::put32(p, 8);
        for (unsigned b = 0; b < 8; ++b) *p++ = (unsigned char)(0xA0 + i);
    }

    save::put32(p, kRecordId);
    save::put32(p, 1);
    save::put32(p, 9);
    save::put32(p, 16);
    save::put32(p, 2);
    save::put32(p, 4);
    save::put32(p, 1);
    save::put32(p, 0);
    save::put32(p, 8);
    save::put32(p, 2);
    save::put32(p, 0);
    store(p + 0, 0x00658EF8);
    store(p + 4, 0);
    store(p + 8, 0);
    store(p + 12, 0x00000005);
    return (unsigned)(p + 16 - out);
}

}

int main() {
    unsigned char file[512] = {};
    const unsigned size = build_file(file);
    CHECK(size < sizeof file);

    save::Header h = {};
    CHECK(save::read_header(file, size, &h));
    CHECK(std::memcmp(h.magic, save::kMagic, 8) == 0);
    CHECK(h.version == save::kVersion);
    CHECK(h.record_count == 1 && h.class_count == 1);
    CHECK(h.section_count == save::kSectionCount);

    bool leaked = false;
    for (unsigned i = 0; i + 4 <= size; ++i) {
        uint32_t v = 0;
        std::memcpy(&v, file + i, 4);
        if (v == kSaverBlock1 || v == kSaverBlock2) leaked = true;
    }
    CHECK(!leaked);

    save::RefTable refs;
    refs.clear();
    CHECK(refs.add(1, kLoaderBlock1));
    CHECK(refs.add(2, kLoaderBlock2));
    CHECK(!refs.add(2, kLoaderBlock1));
    unsigned char body[16];
    std::memcpy(body, file + size - 16, 16);
    const save::Patch patches[2] = {{4, 1, 0}, {8, 2, 0}};
    CHECK(save::resolve_body(refs, body, sizeof body, patches, 2));
    uint32_t at4 = 0, at8 = 0, at12 = 0;
    std::memcpy(&at4, body + 4, 4);
    std::memcpy(&at8, body + 8, 4);
    std::memcpy(&at12, body + 12, 4);
    CHECK(at4 == kLoaderBlock1);
    CHECK(at8 == kLoaderBlock2);
    CHECK(at12 == 5);
    CHECK(*(uint32_t*)body == 0x00658EF8);

    save::RefTable partial;
    partial.clear();
    CHECK(partial.add(1, kLoaderBlock1));
    unsigned char bodysick[16] = {};
    CHECK(!save::resolve_body(partial, bodysick, sizeof bodysick, patches, 2));

    const save::Patch bad[1] = {{12, 1, 0}};
    unsigned char body2[16] = {};
    CHECK(!save::resolve_body(refs, body2, 8, bad, 1));
    save::Header badHeader = {};
    std::memcpy(badHeader.magic, save::kMagic, 8);
    badHeader.version = save::kVersion + 1;
    badHeader.header_bytes = save::kHeaderBytes;
    badHeader.section_count = save::kSectionCount;
    unsigned char badFile[64] = {};
    unsigned char* q = badFile;
    save::write_header(q, badHeader);
    CHECK(!save::read_header(badFile, sizeof badFile, &h));
    CHECK(!save::read_header(file, save::kHeaderBytes - 8, &h));

    if (g_failures) {
        std::fprintf(stderr, "%u failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok save format: header, class map, %u blocks, %u patches, references resolved\n",
                2u, 2u);
    return 0;
}
