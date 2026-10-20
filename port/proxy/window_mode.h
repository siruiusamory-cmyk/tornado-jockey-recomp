
#pragma once

#include <cstdlib>
#include <cstring>

namespace windowmode {

enum Kind { kWindowed, kBorderless, kDesktop };

inline Kind kind(const char* text) {
    if (text && !std::strcmp(text, "borderless")) return kBorderless;
    if (text && !std::strcmp(text, "fullscreen-desktop")) return kDesktop;
    return kWindowed;
}

inline unsigned factor(const char* text, unsigned back_w, unsigned back_h, unsigned work_w,
                       unsigned work_h) {
    if (text && !std::strcmp(text, "fit")) {
        if (!back_w || !back_h) return 1;
        unsigned fx = work_w / back_w;
        unsigned fy = work_h / back_h;
        unsigned f = fx < fy ? fx : fy;
        return f ? f : 1;
    }
    unsigned f = text ? (unsigned)std::atoi(text) : 1;
    if (f < 1) f = 1;
    if (f > 8) f = 8;
    return f;
}

struct Plan {
    unsigned width;
    unsigned height;
    bool popup;
};

inline Plan plan(Kind k, unsigned factor, unsigned back_w, unsigned back_h, unsigned work_w,
                 unsigned work_h, unsigned screen_w, unsigned screen_h) {
    Plan p = {back_w * factor, back_h * factor, false};
    if (k == kBorderless) {
        p.width = work_w;
        p.height = work_h;
        p.popup = true;
    } else if (k == kDesktop) {
        p.width = screen_w;
        p.height = screen_h;
        p.popup = true;
    }
    return p;
}

struct Rect {
    long left;
    long top;
    long right;
    long bottom;
};

inline Rect letterbox(unsigned back_w, unsigned back_h, long client_w, long client_h) {
    Rect r = {0, 0, client_w, client_h};
    if (!back_w || !back_h || client_w <= 0 || client_h <= 0) return r;
    double sx = (double)client_w / (double)back_w;
    double sy = (double)client_h / (double)back_h;
    double s = sx < sy ? sx : sy;
    long w = (long)(back_w * s + 0.5);
    long h = (long)(back_h * s + 0.5);
    r.left = (client_w - w) / 2;
    r.top = (client_h - h) / 2;
    r.right = r.left + w;
    r.bottom = r.top + h;
    return r;
}

}
