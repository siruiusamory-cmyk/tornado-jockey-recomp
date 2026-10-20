
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace replay {

constexpr int64_t kQpf = 60000000;
constexpr int64_t kFiletimePerSecond = 10000000;

enum class Parse { Event, Skip, Bad };

struct Event {
    uint64_t frame;
    char name[24];
    long long arg[3];
    unsigned nargs;
};

inline const char* skip_space(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
    return p;
}

inline bool token(const char** p, char* out, unsigned size) {
    const char* q = skip_space(*p);
    if (!*q) {
        *p = q;
        return false;
    }
    const char* start = q;
    while (*q && *q != ' ' && *q != '\t' && *q != '\r') ++q;
    unsigned len = (unsigned)(q - start);
    *p = q;
    if (len >= size) return false;
    std::memcpy(out, start, len);
    out[len] = 0;
    return true;
}

inline Parse parse_line(const char* line, Event* out) {
    std::memset(out, 0, sizeof *out);
    const char* p = skip_space(line);
    if (!*p || *p == '#') return Parse::Skip;
    char word[16];
    if (!token(&p, word, (unsigned)sizeof word) || std::strcmp(word, "frame") != 0) {
        return Parse::Bad;
    }
    char number[24];
    if (!token(&p, number, (unsigned)sizeof number) || number[0] == '-') return Parse::Bad;
    char* end = nullptr;
    unsigned long long frame = std::strtoull(number, &end, 10);
    if (end == number || *end) return Parse::Bad;
    if (!token(&p, out->name, (unsigned)sizeof out->name)) return Parse::Bad;
    out->frame = frame;
    char arg[32];
    while (out->nargs < 3 && token(&p, arg, (unsigned)sizeof arg)) {
        end = nullptr;
        long long value = std::strtoll(arg, &end, 0);
        if (end == arg || *end) return Parse::Bad;
        out->arg[out->nargs++] = value;
    }
    if (*skip_space(p)) return Parse::Bad;
    return Parse::Event;
}

inline bool parse(const char* text, Event* events, unsigned capacity, unsigned* count) {
    unsigned used = 0;
    const char* p = text;
    while (*p) {
        const char* end = p;
        while (*end && *end != '\n') ++end;
        char line[128];
        unsigned len = (unsigned)(end - p);
        if (len >= sizeof line) {
            if (count) *count = used;
            return false;
        }
        std::memcpy(line, p, len);
        line[len] = 0;
        Event event;
        Parse state = parse_line(line, &event);
        if (state == Parse::Bad || (state == Parse::Event && used >= capacity)) {
            if (count) *count = used;
            return false;
        }
        if (state == Parse::Event) events[used++] = event;
        p = *end ? end + 1 : end;
    }
    if (count) *count = used;
    return true;
}

struct State {
    long dx;
    long dy;
    unsigned buttons;
    unsigned char keys[256];
    long pov;
    unsigned joy_buttons;
};

inline unsigned apply_due(const Event* events, unsigned count, unsigned* next, uint64_t frame,
                          State* state, unsigned* activates) {
    unsigned applied = 0;
    while (*next < count && events[*next].frame <= frame) {
        const Event& event = events[*next];
        if (std::strcmp(event.name, "activate") == 0) {
            ++*activates;
        } else if (std::strcmp(event.name, "mouse") == 0 && event.nargs >= 2) {
            state->dx += (long)event.arg[0];
            state->dy += (long)event.arg[1];
        } else if (std::strcmp(event.name, "button") == 0 && event.nargs >= 2) {
            unsigned index = (unsigned)event.arg[0];
            if (index < 4) {
                if (event.arg[1]) {
                    state->buttons |= 1u << index;
                } else {
                    state->buttons &= ~(1u << index);
                }
            }
        } else if (std::strcmp(event.name, "key") == 0 && event.nargs >= 2) {
            unsigned key = (unsigned)event.arg[0];
            if (key < 256) state->keys[key] = event.arg[1] ? 0x80 : 0;
        } else if (std::strcmp(event.name, "joy") == 0 && event.nargs >= 2) {
            state->pov = (long)event.arg[0];
            state->joy_buttons = (unsigned)event.arg[1];
        }
        ++*next;
        ++applied;
    }
    return applied;
}

inline void take_mouse(State* state, long* dx, long* dy) {
    *dx = state->dx;
    *dy = state->dy;
    state->dx = 0;
    state->dy = 0;
}

inline int64_t qpf() { return kQpf; }

constexpr int64_t kTick = kQpf / 60000;

inline bool& clock_live() {
    static bool live = false;
    return live;
}

inline int64_t& clock_seed() {
    static int64_t seed = 0;
    return seed;
}

inline int64_t& clock_ticks() {
    static int64_t ticks = 0;
    return ticks;
}

constexpr int64_t kSeed = 3000000000000;
constexpr int64_t kFiletimeBase = 127805472000000000;

struct Guid {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
};
constexpr Guid kBrowserClsid = {0x8856f961u, 0x340au, 0x11d0,
                                {0xa9, 0x6b, 0x00, 0xc0, 0x4f, 0xd7, 0x05, 0xa2}};
constexpr long kCursorX = 0;
constexpr long kCursorY = 0;

inline bool is_browser_clsid(const Guid* id) {
    return id && std::memcmp(id, &kBrowserClsid, sizeof kBrowserClsid) == 0;
}

inline int64_t clock_qpc() { return clock_seed() + (clock_ticks() += kTick); }

inline int64_t clock_ms(int which) {
    static int64_t time_get_time = 0;
    static int64_t get_tick_count = 0;
    int64_t& counter = which ? get_tick_count : time_get_time;
    return counter += 1000 / 60;
}

inline int64_t& clock_filetime_base() {
    static int64_t base = 0;
    return base;
}

inline int64_t& clock_filetime_ticks() {
    static int64_t ticks = 0;
    return ticks;
}

inline int64_t clock_filetime() {
    return clock_filetime_base() + (clock_filetime_ticks() += kFiletimePerSecond / 60);
}

struct Posted {
    unsigned lparam[8];
    unsigned head;
    unsigned count;
};

inline void post_add(Posted* posted, unsigned lparam) {
    if (posted->count == sizeof posted->lparam / sizeof posted->lparam[0]) return;
    posted->lparam[(posted->head + posted->count) % 8] = lparam;
    ++posted->count;
}

inline bool post_take(Posted* posted, unsigned lparam) {
    if (!posted->count || posted->lparam[posted->head] != lparam) return false;
    posted->head = (posted->head + 1) % 8;
    --posted->count;
    return true;
}

inline int64_t clock_filetime_now() {
    return clock_filetime_base() + clock_filetime_ticks();
}

inline void clock_start(int64_t origin) {
    clock_seed() = origin;
    clock_ticks() = 0;
    clock_filetime_ticks() = 0;
    clock_live() = true;
}

}
