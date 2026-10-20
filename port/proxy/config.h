
#pragma once

#include <cstddef>
#include <cstring>

namespace config {

constexpr const char* kFileName = "d3d8.conf";

constexpr const char* kDefault =
    "# Tornado Jockey proxy settings, read once when the proxy loads.\n"
    "# faithful=1: every port addition is off and the game behaves as it did in 2005.\n"
    "# faithful=0: additions on, which is what the keyboard steering and the fixed 1/60\n"
    "# step need.\n"
    "# window=windowed|borderless|fullscreen-desktop and scale=1..8|fit size and place the\n"
    "# window; the game still draws its 600x450 frame and the present scales it.\n"
    "# step=0 keeps the original variable step (the translation comparison needs it).\n"
    "faithful=0\n"
    "window=windowed\n"
    "scale=1\n"
    "step=1\n";

inline int faithful(const char* text) {
    int value = 0;
    for (const char* p = text; p && *p;) {
        const char* end = p;
        while (*end && *end != '\n') ++end;
        const char* q = p;
        while (q < end && (*q == ' ' || *q == '\t')) ++q;
        const char* key = "faithful=";
        const char* k = key;
        while (*k && q < end && *q == *k) {
            ++k;
            ++q;
        }
        if (!*k) value = q < end && *q == '1' ? 1 : 0;
        p = *end ? end + 1 : end;
    }
    return value;
}

inline bool value(const char* text, const char* key, char* out, size_t size) {
    if (!out || !size) return false;
    out[0] = 0;
    size_t keylen = std::strlen(key);
    bool found = false;
    for (const char* p = text; p && *p;) {
        const char* end = p;
        while (*end && *end != '\n') ++end;
        const char* q = p;
        while (q < end && (*q == ' ' || *q == '\t')) ++q;
        if ((size_t)(end - q) >= keylen && !std::strncmp(q, key, keylen)) {
            q += keylen;
            const char* e = end;
            if (e > q && e[-1] == '\r') --e;
            size_t n = (size_t)(e - q);
            if (n >= size) n = size - 1;
            std::memcpy(out, q, n);
            out[n] = 0;
            found = true;
        }
        p = *end ? end + 1 : end;
    }
    return found;
}

inline int flag(const char* text, const char* key, int fallback) {
    char v[16] = {};
    if (!value(text, key, v, sizeof v)) return fallback;
    return v[0] == '1' ? 1 : 0;
}

}
