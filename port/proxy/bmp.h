
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bmp {

inline void put16(unsigned char* at, uint16_t value) { std::memcpy(at, &value, 2); }
inline void put32(unsigned char* at, uint32_t value) { std::memcpy(at, &value, 4); }

inline bool write_24(const char* path, unsigned width, unsigned height,
                     const unsigned char* top_left, int pitch) {
    if (!path || !width || !height || !top_left || pitch < (int)width * 4) return false;
    const uint32_t stride = (width * 3 + 3) & ~3u;
    unsigned char header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    put32(header + 2, 54 + stride * height);
    put32(header + 10, 54);
    put32(header + 14, 40);
    put32(header + 18, width);
    put32(header + 22, height);
    put16(header + 26, 1);
    put16(header + 28, 24);
    put32(header + 34, stride * height);
    unsigned char* row = static_cast<unsigned char*>(std::malloc(stride));
    if (!row) return false;
    FILE* file = std::fopen(path, "wb");
    bool ok = file && std::fwrite(header, 1, sizeof header, file) == sizeof header;
    for (unsigned r = 0; ok && r < height; ++r) {
        const unsigned char* src = top_left + (height - 1 - r) * (size_t)pitch;
        std::memset(row, 0, stride);
        for (unsigned x = 0; x < width; ++x) std::memcpy(row + x * 3, src + x * 4, 3);
        ok = std::fwrite(row, 1, stride, file) == stride;
    }
    if (file) ok = std::fclose(file) == 0 && ok;
    std::free(row);
    return ok;
}

}
