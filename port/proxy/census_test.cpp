
#include "census.h"

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

void put32(unsigned char* p, uint32_t v) { std::memcpy(p, &v, 4); }

struct Window {
    uint32_t base;
    unsigned char* data;
    unsigned size;
};

unsigned char g_table[0x100];
unsigned char g_code[0x4000];
constexpr uint32_t kCode = 0x100000;

const Window kWindows[] = {
    {census::kFactoryTable, g_table, sizeof g_table},
    {kCode, g_code, sizeof g_code},
};

const Window* find(uint32_t address, unsigned size) {
    for (const Window& w : kWindows) {
        if (address >= w.base && address + size <= w.base + w.size) return &w;
    }
    return nullptr;
}

bool read_dword(uint32_t address, uint32_t* out) {
    const Window* w = find(address, 4);
    if (!w) return false;
    std::memcpy(out, w->data + (address - w->base), 4);
    return true;
}

bool read_bytes(uint32_t address, void* out, unsigned size) {
    const Window* w = find(address, size);
    if (!w) return false;
    std::memcpy(out, w->data + (address - w->base), size);
    return true;
}

unsigned char* at(uint32_t address) {
    const Window* w = find(address, 1);
    return w ? w->data + (address - w->base) : nullptr;
}

void factory_bytes(uint32_t base, uint32_t size, uint32_t ctor) {
    unsigned char* code = at(base);
    std::memset(code, 0x90, census::kFactoryBytes);
    unsigned i = 0;
    code[i++] = 0x68;
    put32(code + i, 0x5e71bb);
    i += 4;
    code[i++] = 0x68;
    put32(code + i, size);
    i += 4;
    code[i++] = 0xE8;
    put32(code + i, census::kAllocator - (base + i + 4));
    i += 4;
    code[i++] = 0xE8;
    put32(code + i, ctor - (base + i + 4));
}

void ctor_bytes(uint32_t base, uint32_t vtable) {
    unsigned char* code = at(base);
    std::memset(code, 0x90, census::kCtorBytes);
    code[0] = 0x55;
    code[1] = 0x8B;
    code[2] = 0xEC;
    code[3] = 0xC7;
    code[4] = 0x45;
    code[5] = 0x00;
    put32(code + 6, vtable);
}

void cases_alloc_size() {
    uint32_t base = kCode + 0x1000;
    factory_bytes(base, 0x2C58, kCode + 0x2000);
    unsigned char code[census::kFactoryBytes] = {};
    std::memcpy(code, at(base), sizeof code);
    uint32_t size = 0, ctor = 0;
    unsigned end = 0;
    CHECK(census::alloc_size(code, sizeof code, base, &size, &end));
    CHECK(size == 0x2C58);
    CHECK(census::ctor_call(code, sizeof code, base, end, &ctor));
    CHECK(ctor == kCode + 0x2000);

    std::memset(code, 0x90, sizeof code);
    CHECK(!census::alloc_size(code, sizeof code, base, &size, &end));
}

void cases_vtable_store() {
    unsigned char code[census::kCtorBytes] = {};
    uint32_t vtable = 0;
    ctor_bytes(kCode + 0x100, 0x658EF8);
    std::memcpy(code, at(kCode + 0x100), sizeof code);
    CHECK(census::vtable_store(code, sizeof code, &vtable));
    CHECK(vtable == 0x658EF8);

    std::memset(code, 0x90, sizeof code);
    code[0] = 0xC7;
    code[1] = 0x06;
    put32(code + 2, 0x65C700);
    vtable = 0;
    CHECK(census::vtable_store(code, sizeof code, &vtable));
    CHECK(vtable == 0x65C700);

    std::memset(code, 0x90, sizeof code);
    code[0] = 0xC7;
    code[1] = 0x07;
    put32(code + 2, 0x00401000);
    vtable = 0;
    CHECK(!census::vtable_store(code, sizeof code, &vtable));
    CHECK(vtable == 0);
}

void cases_decode() {
    std::memset(g_table, 0, sizeof g_table);
    std::memset(g_code, 0, sizeof g_code);
    const uint32_t kFactory1 = kCode + 0x3000, kFactory2 = kCode + 0x3100;
    const uint32_t kCtor1 = kCode + 0x200, kCtor2 = kCode + 0x300;

    put32(g_table + 4, kFactory1);
    put32(g_table + 8, kFactory2);
    factory_bytes(kFactory1, 0xDDC, kCtor1);
    factory_bytes(kFactory2, 0x2C58, kCtor2);
    ctor_bytes(kCtor1, 0x657BB8);
    ctor_bytes(kCtor2, 0x658EF8);

    census::Class list[census::kMaxClasses] = {};
    unsigned n = census::decode(read_dword, read_bytes, list, census::kMaxClasses);
    CHECK(n == 2);
    CHECK(list[0].id == 1);
    CHECK(list[0].size == 0xDDC);
    CHECK(list[0].vtable == 0x657BB8);
    CHECK(list[1].id == 2);
    CHECK(list[1].size == 0x2C58);
    CHECK(list[1].vtable == 0x658EF8);

    CHECK(census::by_vtable(list, n, 0x658EF8) == &list[1]);
    CHECK(census::by_vtable(list, n, 0x657BB8) == &list[0]);
    CHECK(census::by_vtable(list, n, 0) == nullptr);
    CHECK(census::by_vtable(list, n, 0x12345678) == nullptr);

    std::memset(at(kFactory1), 0x90, census::kFactoryBytes);
    n = census::decode(read_dword, read_bytes, list, census::kMaxClasses);
    CHECK(n == 1);
    CHECK(list[0].size == 0x2C58);
}

}

int main() {
    cases_alloc_size();
    cases_vtable_store();
    cases_decode();
    if (g_failures) return 1;
    std::printf("ok class decode cases\n");
    return 0;
}
