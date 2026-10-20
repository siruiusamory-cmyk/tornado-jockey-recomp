
#pragma once

#include <atomic>

namespace render {

enum : unsigned {
    kRsZEnable = 7,
    kRsFillMode = 8,
    kRsZWrite = 14,
    kRsAlphaTest = 15,
    kRsSrcBlend = 19,
    kRsDestBlend = 20,
    kRsCullMode = 22,
    kRsZFunc = 23,
    kRsAlphaRef = 24,
    kRsAlphaFunc = 25,
    kRsBlendEnable = 27,
    kRsFogEnable = 28,
    kRsFogColor = 34,
    kRsFogTableMode = 35,
    kRsFogStart = 36,
    kRsFogEnd = 37,
    kRsTextureFactor = 60,
    kRsClipping = 136,
    kRsColorVertex = 141,
    kRsLighting = 137,
    kRsAmbient = 139,
    kRsFogVertexMode = 140,
    kRsDiffuseSource = 145,
    kRsSpecularSource = 146,
    kRsAmbientSource = 147,
    kRsBlendOp = 171,
};

constexpr unsigned kCreateFpuPreserve = 0x2;

inline unsigned primitive_vertices(unsigned primitive, unsigned primitives) {
    if (primitive == 4) return primitives * 3;
    if (primitive == 5 || primitive == 6) return primitives ? primitives + 2 : 0;
    return 0;
}

inline unsigned expand_indices(unsigned primitive, const unsigned* indexList, unsigned vertices,
                               unsigned* out, unsigned capacity) {
    if (!out) return 0;
    if (primitive == 4) {
        if (vertices > capacity) return 0;
        for (unsigned i = 0; i < vertices; ++i) out[i] = indexList ? indexList[i] : i;
        return vertices;
    }
    if (primitive != 5 && primitive != 6) return 0;
    unsigned triangles = vertices > 2 ? vertices - 2 : 0;
    if (triangles * 3u > capacity) return 0;
    unsigned at = 0;
    for (unsigned t = 0; t < triangles; ++t) {
        unsigned a = primitive == 5 ? (indexList ? indexList[t] : t)
                                    : (indexList ? indexList[0] : 0u);
        unsigned b = indexList ? indexList[t + 1] : t + 1;
        unsigned c = indexList ? indexList[t + 2] : t + 2;
        if (primitive == 5 && (t & 1)) {
            out[at++] = b;
            out[at++] = a;
            out[at++] = c;
        } else {
            out[at++] = a;
            out[at++] = b;
            out[at++] = c;
        }
    }
    return at;
}

inline unsigned texcoord_set(unsigned tci) { return tci & 0xFFFFu; }
inline unsigned texcoord_source(unsigned tci) { return tci >> 16; }

inline unsigned arg_selector(unsigned arg) { return arg & 15u; }
inline bool arg_complement(unsigned arg) { return (arg & 0x10u) != 0; }
inline bool arg_alpha_replicate(unsigned arg) { return (arg & 0x20u) != 0; }
inline bool arg_modified(unsigned arg) { return (arg & 0x30u) != 0; }

inline float fog_amount(bool enabled, float start, float end, float depth) {
    if (!enabled || !(end > start)) return 1.0f;
    float f = (end - depth) / (end - start);
    return f < 0 ? 0.0f : (f > 1 ? 1.0f : f);
}

inline unsigned effective_stages(unsigned op0, unsigned op1) {
    if (op0 == 1) return 0;
    return op1 == 1 ? 1 : 2;
}

inline bool index_overruns(unsigned startIndex, unsigned vertices, unsigned length,
                           unsigned format) {
    unsigned width = format == 102 ? 4u : 2u;
    return ((unsigned long long)startIndex + vertices) * width > length;
}

inline unsigned clamp_index_count(unsigned startIndex, unsigned vertices, unsigned length,
                                  unsigned format) {
    unsigned available = length / (format == 102 ? 4u : 2u);
    if (startIndex >= available) return 0;
    unsigned room = available - startIndex;
    return vertices < room ? vertices : room;
}

inline bool fog_mode_linear(unsigned mode) { return mode == 0 || mode == 3; }

inline unsigned fog_selected_mode(unsigned tableMode, unsigned vertexMode) {
    return tableMode ? tableMode : vertexMode;
}

inline bool fog_applies(bool enabled, unsigned tableMode, unsigned vertexMode) {
    if (!enabled) return false;
    unsigned mode = fog_selected_mode(tableMode, vertexMode);
    return mode != 0 && fog_mode_linear(mode);
}

inline unsigned blend_op_index(unsigned op) { return op >= 1 && op <= 5 ? op - 1 : 0; }

inline bool light_uses_position(unsigned type) { return type != 3; }

inline unsigned mip_filter_flag(unsigned stage, unsigned mipFilter, bool bound) {
    return bound && mipFilter == 0 ? 128u << stage : 0u;
}

inline bool alpha_pass(unsigned func, unsigned alpha8, unsigned ref8) {
    switch (func) {
        case 1: return false;
        case 2: return alpha8 < ref8;
        case 3: return alpha8 == ref8;
        case 4: return alpha8 <= ref8;
        case 5: return alpha8 > ref8;
        case 6: return alpha8 != ref8;
        case 7: return alpha8 >= ref8;
        default: return true;
    }
}

inline unsigned long long pso_key(bool offscreen, const unsigned* rs) {
    unsigned long long key = offscreen ? 1u : 0u;
    key |= (unsigned long long)(rs[kRsZEnable] ? 1 : 0) << 2;
    key |= (unsigned long long)(rs[kRsZWrite] ? 1 : 0) << 3;
    key |= (unsigned long long)(rs[kRsZFunc] & 0xF) << 4;
    key |= (unsigned long long)(rs[kRsCullMode] & 0x7) << 8;
    key |= (unsigned long long)(rs[kRsFillMode] & 0x3) << 11;
    key |= (unsigned long long)(rs[kRsBlendEnable] ? 1 : 0) << 13;
    key |= (unsigned long long)(rs[kRsSrcBlend] & 0xF) << 14;
    key |= (unsigned long long)(rs[kRsDestBlend] & 0xF) << 18;
    key |= (unsigned long long)(rs[kRsBlendOp] & 0x7) << 22;
    key |= (unsigned long long)(rs[kRsAlphaTest] ? 1 : 0) << 25;
    key |= (unsigned long long)(rs[kRsAlphaFunc] & 0xF) << 26;
    key |= (unsigned long long)(rs[kRsAlphaRef] & 0xFF) << 30;
    key |= (unsigned long long)(rs[kRsClipping] ? 1 : 0) << 38;
    return key;
}

constexpr unsigned kDescriptors = 16384;
constexpr unsigned kPairBase = 1;
constexpr unsigned kPairTop = 8192;
inline unsigned pair_capacity() { return (kPairTop - kPairBase) / 2; }
inline unsigned pair_base(unsigned index) { return kPairBase + 2 * index; }
inline unsigned texture_capacity() { return kDescriptors - kPairTop; }

struct SrvSlots {
    unsigned top = kDescriptors - 1;
    unsigned free[kDescriptors - kPairTop] = {};
    unsigned freeCount = 0;
    unsigned reused = 0, exhausted = 0;

    unsigned alloc() {
        if (freeCount) {
            ++reused;
            return free[--freeCount];
        }
        if (top < kPairTop) {
            ++exhausted;
            return 0;
        }
        return top--;
    }
    void release(unsigned index) {
        if (index >= kPairTop && index < kDescriptors && freeCount < texture_capacity())
            free[freeCount++] = index;
    }
};

struct ThreadGuard {
    std::atomic<unsigned> violations{0};
    std::atomic<bool> failed{false};
    std::atomic<unsigned> owner{0};

    void own(unsigned self) { owner.store(self, std::memory_order_relaxed); }
    unsigned known_owner() const { return owner.load(std::memory_order_relaxed); }
    unsigned count() const { return violations.load(std::memory_order_relaxed); }
    bool frame_failed() const { return failed.load(std::memory_order_relaxed); }

    bool enter(unsigned self, bool& first) {
        first = false;
        unsigned at = known_owner();
        if (!at || self == at) return true;
        first = violations.fetch_add(1, std::memory_order_relaxed) == 0;
        failed.store(true, std::memory_order_relaxed);
        return false;
    }
};

struct RetireQueue {
    struct Entry {
        unsigned long long resource;
        unsigned srv;
        unsigned long long fence;
    };
    static constexpr unsigned kSlots = 256;
    Entry table[kSlots] = {};
    Entry pending[kSlots] = {};
    unsigned count = 0, pendingCount = 0, dropped = 0;

    unsigned store(Entry* where, unsigned& n, unsigned long long fence,
                   unsigned long long resource, unsigned srv) {
        if (n == kSlots) return ++dropped;
        where[n].resource = resource;
        where[n].srv = srv;
        where[n].fence = fence;
        ++n;
        return 0;
    }
    unsigned push(bool listOpen, unsigned long long lastFence, unsigned long long resource,
                  unsigned srv) {
        if (!resource && !srv) return 0;
        return listOpen ? store(pending, pendingCount, lastFence, resource, srv)
                        : store(table, count, lastFence, resource, srv);
    }

    unsigned push_fenced(unsigned long long fence, unsigned long long resource, unsigned srv) {
        if (!resource && !srv) return 0;
        return store(table, count, fence, resource, srv);
    }

    unsigned bind(unsigned long long fence) {
        unsigned lost = 0;
        for (unsigned i = 0; i < pendingCount; ++i) {
            if (store(table, count, fence, pending[i].resource, pending[i].srv)) ++lost;
        }
        pendingCount = 0;
        return lost;
    }

    unsigned drain_pending(bool nothingCanRead, Entry* out, unsigned capacity) {
        if (!nothingCanRead || pendingCount > capacity) return 0;
        for (unsigned i = 0; i < pendingCount; ++i) out[i] = pending[i];
        unsigned n = pendingCount;
        pendingCount = 0;
        return n;
    }

    static bool release_pending_on_complete(bool waitCompleted, bool& transferPending) {
        if (waitCompleted) transferPending = false;
        return waitCompleted;
    }

    unsigned drain(unsigned long long completed, Entry* out, unsigned capacity) {
        unsigned kept = 0, ready = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (table[i].fence > completed) {
                table[kept++] = table[i];
            } else if (ready < capacity) {
                out[ready++] = table[i];
            }
        }
        count = kept;
        return ready;
    }
};

}
