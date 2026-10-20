
#pragma once

namespace steer {

enum Key : unsigned {
    kLeft = 1,
    kRight = 2,
    kUp = 4,
    kDown = 8,
};

constexpr long kPxPerPresent = 5;

inline void direction(unsigned mask, int* dx, int* dy) {
    *dx = (mask & kRight ? 1 : 0) - (mask & kLeft ? 1 : 0);
    *dy = (mask & kDown ? 1 : 0) - (mask & kUp ? 1 : 0);
}

inline void step(long* x, long* y, int dx, int dy, long width, long height) {
    if (width > 0) {
        *x += dx * kPxPerPresent;
        if (*x < 0) *x = 0;
        if (*x > width - 1) *x = width - 1;
    }
    if (height > 0) {
        *y += dy * kPxPerPresent;
        if (*y < 0) *y = 0;
        if (*y > height - 1) *y = height - 1;
    }
}

inline unsigned long lparam(long x, long y) {
    return ((unsigned long)(unsigned short)y << 16) | (unsigned short)x;
}

}
