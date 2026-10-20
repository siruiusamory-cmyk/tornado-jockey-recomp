
#include "probe_list.h"

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

struct Want {
    uint32_t address;
    uint32_t offset;
    bool indirect;
    const char* text;
    uint32_t index_address;
    uint32_t index_offset;
    uint32_t field;
    bool has_index;
    bool index_indirect;
};

struct AcceptCase {
    const char* input;
    unsigned count;
    Want want[4];
};

const AcceptCase kAccept[] = {
    {"0x9FEFC4", 1, {{0x9FEFC4u, 0, false, "0x9FEFC4"}}},
    {"9fefc4", 1, {{0x9fefc4u, 0, false, "9fefc4"}}},
    {"DEADBEEF", 1, {{0xDEADBEEFu, 0, false, "DEADBEEF"}}},
    {"0x9FEFC4+0x58", 1, {{0x9FEFC4u, 0x58, true, "0x9FEFC4+0x58"}}},
    {"0x9FEFC4+0", 1, {{0x9FEFC4u, 0, true, "0x9FEFC4+0"}}},
    {"0xAbCdEf", 1, {{0xABCDEFu, 0, false, "0xAbCdEf"}}},
    {"  0x10 , 0x20 ,0x30  ", 3, {{0x10, 0, false, "0x10"},
                                  {0x20, 0, false, "0x20"},
                                  {0x30, 0, false, "0x30"}}},
    {"6D15E0+EC94@6D09D4+1E78.261C", 1,
     {{0x6D15E0u, 0xEC94, true, "6D15E0+EC94@6D09D4+1E78.261C", 0x6D09D4u, 0x1E78, 0x261C, true,
       true}}},
    {"0x1234@0x5678.0", 1,
     {{0x1234, 0, false, "0x1234@0x5678.0", 0x5678, 0, 0, true, false}}},
    {"0x10DA5580+0x0@0x0+0x0.0x0", 1,
     {{0x10DA5580u, 0, true, "0x10DA5580+0x0@0x0+0x0.0x0", 0, 0, 0, true, true}}},
};

const char* const kReject[] = {
    "",
    ",",
    "0x9FEFC4,",
    "0x9FEFC4 junk",
    "0xg",
    "-1",
    "0x9FEFC4+",
    "0x9FEFC4 +0x58",
    "0x9FEFC4,,0x10",

    "0000000000000000000000000000000000000000",

    "00000000000000000000000000000000000000000",
    "6D15E0@.261C",
    "@6D09D4.1E78",
    "6D15E0+EC94@6D09D4+1E78",
    "6D15E0@6D09D4.261C.261C",
    "a@b@c.d",
    "6D15E0+EC94@6D09D4.",
};

void test_accept() {
    for (const AcceptCase& c : kAccept) {
        probe::Term terms[probe::kMaxTerms] = {};
        unsigned count = 0;
        CHECK(probe::parse(c.input, terms, probe::kMaxTerms, &count));
        CHECK(count == c.count);
        for (unsigned i = 0; i < c.count && i < 4; ++i) {
            CHECK(terms[i].address == c.want[i].address);
            CHECK(terms[i].offset == c.want[i].offset);
            CHECK(terms[i].indirect == c.want[i].indirect);
            CHECK(terms[i].has_index == c.want[i].has_index);
            CHECK(terms[i].index_address == c.want[i].index_address);
            CHECK(terms[i].index_offset == c.want[i].index_offset);
            CHECK(terms[i].index_indirect == c.want[i].index_indirect);
            CHECK(terms[i].field == c.want[i].field);
            CHECK(std::strcmp(terms[i].text, c.want[i].text) == 0);
        }
    }
}

void test_reject() {
    for (const char* input : kReject) {
        probe::Term terms[probe::kMaxTerms] = {};
        unsigned count = 99;
        CHECK(!probe::parse(input, terms, probe::kMaxTerms, &count));
        CHECK(count == 0);
    }
}

void test_limits() {
    char list[2 * probe::kMaxTerms + 1] = {};
    for (unsigned i = 0; i < probe::kMaxTerms; ++i) {
        if (i) std::strcat(list, ",");
        std::strcat(list, "0");
    }
    probe::Term terms[probe::kMaxTerms] = {};
    unsigned count = 0;
    CHECK(probe::parse(list, terms, probe::kMaxTerms, &count));
    CHECK(count == probe::kMaxTerms);

    std::strcat(list, ",0");
    count = 99;
    CHECK(!probe::parse(list, terms, probe::kMaxTerms, &count));
    CHECK(count == 0);

    count = 99;
    CHECK(!probe::parse("1,2,3", terms, 2, &count));
    CHECK(count == 0);
    CHECK(probe::parse("1,2,3", terms, 3, &count));
    CHECK(count == 3);
}

}

int main() {
    test_accept();
    test_reject();
    test_limits();
    if (g_failures) {
        std::fprintf(stderr, "%u case(s) failed\n", g_failures);
        return 1;
    }
    std::printf("ok %u parser cases\n",
                (unsigned)(sizeof kAccept / sizeof kAccept[0] +
                           sizeof kReject / sizeof kReject[0]));
    return 0;
}
