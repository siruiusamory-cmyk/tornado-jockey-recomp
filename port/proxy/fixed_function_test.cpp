
#include "render/fixed_function.h"

#include <windows.h>

#undef small

#include <cstdio>
#include <cstring>
#include <thread>

#ifdef TJ_RED_HISTORY
#include "legacy_rules.h"
namespace rules {

inline bool alpha_pass(unsigned func, unsigned alpha8, unsigned ref8) {
    if (func == 7) return alpha8 >= ref8;
    if (func == 5) return alpha8 > ref8;
    return true;
}

inline unsigned effective_stages(unsigned op0, unsigned op1) {
    unsigned stages = 0;
    if (op0 != 1) ++stages;
    if (op1 != 1) ++stages;
    return stages;
}

inline bool fog_mode_linear(unsigned mode) {
    (void)mode;
    return true;
}

inline bool fog_applies(bool enabled, unsigned tableMode, unsigned vertexMode) {
    (void)tableMode;
    (void)vertexMode;
    return enabled;
}

inline unsigned blend_op_index(unsigned op) {
    if (op == 4 || op == 5) return 0;
    return op >= 1 && op <= 5 ? op - 1 : 0;
}

inline bool index_overruns(unsigned startIndex, unsigned vertices, unsigned length,
                           unsigned format) {
    (void)startIndex;
    (void)vertices;
    (void)length;
    (void)format;
    return false;
}

inline unsigned clamp_index_count(unsigned, unsigned vertices, unsigned, unsigned) {
    return vertices;
}

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

    unsigned store(unsigned long long fence, unsigned long long resource, unsigned srv) {
        if (count == kSlots) return ++dropped;
        table[count].resource = resource;
        table[count].srv = srv;
        table[count].fence = fence;
        ++count;
        return 0;
    }
    unsigned push(bool listOpen, unsigned long long lastFence, unsigned long long resource,
                  unsigned srv) {
        (void)listOpen;
        if (!resource && !srv) return 0;
        return store(lastFence + 1, resource, srv);
    }
    unsigned push_fenced(unsigned long long fence, unsigned long long resource, unsigned srv) {
        if (!resource && !srv) return 0;
        return store(fence, resource, srv);
    }
    unsigned bind(unsigned long long fence) {
        (void)fence;
        return 0;
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

    unsigned drain_pending(bool, Entry*, unsigned) { return 0; }

    static bool release_pending_on_complete(bool, bool transferPending) { return !transferPending; }
};

struct ThreadGuard {
    unsigned violations = 0;
    bool failed = false;
    unsigned owner = 0;
    void own(unsigned self) { owner = self; }
    unsigned known_owner() const { return owner; }
    unsigned count() const { return violations; }
    bool frame_failed() const { return failed; }
    bool enter(unsigned, bool& first) {
        first = false;
        return true;
    }
};
}
#else
namespace rules = render;
#endif

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char* what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("FAIL %s\n", what);
}

void check_indices(const unsigned* got, unsigned count, const unsigned* want, unsigned wantCount,
                   const char* what) {
    if (count != wantCount) {
        ++failures;
        std::printf("FAIL %s: %u indices, want %u\n", what, count, wantCount);
        return;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (got[i] == want[i]) continue;
        ++failures;
        std::printf("FAIL %s: index %u is %u, want %u\n", what, i, got[i], want[i]);
        return;
    }
    ++checks;
}

#ifdef TJ_RED_HISTORY
unsigned expand_count(unsigned primitive, const unsigned* indexList, unsigned vertices,
                      unsigned* out, unsigned capacity) {

    (void)capacity;
    unsigned triangles = vertices > 2 ? vertices - 2 : 0;
    red::legacy_expand(primitive, indexList, vertices, triangles, out);
    return red::legacy_out_count(primitive, vertices, triangles);
}
#else
unsigned expand_count(unsigned primitive, const unsigned* indexList, unsigned vertices,
                      unsigned* out, unsigned capacity) {
    return render::expand_indices(primitive, indexList, vertices, out, capacity);
}
#endif

void test_state_map() {

    check(render::kRsClipping == 136, "kRsClipping is D3DRS_CLIPPING (136)");
    check(render::kRsAlphaTest == 15, "kRsAlphaTest is D3DRS_ALPHATESTENABLE (15)");
    check(render::kRsSpecularSource == 146, "kRsSpecularSource is D3DRS_SPECULARMATERIALSOURCE");
    check(render::kRsClipping != render::kRsSpecularSource, "clipping does not share a slot");
    check(render::kRsBlendOp == 171, "kRsBlendOp is D3DRS_BLENDOP (171)");
    check(render::kRsFogTableMode == 35, "kRsFogTableMode is D3DRS_FOGTABLEMODE");
    check(render::kRsTextureFactor == 60, "kRsTextureFactor is D3DRS_TEXTUREFACTOR");
}

void test_mip_filter() {
    check(render::mip_filter_flag(0, 0, true) == 128,
          "MIPFILTER NONE on stage 0 selects the base-level sampler");
    check(render::mip_filter_flag(1, 0, true) == 256,
          "MIPFILTER NONE on stage 1 selects the base-level sampler");
    check(render::mip_filter_flag(0, 2, true) == 0, "linear mip filtering keeps the trilinear sampler");
    check(render::mip_filter_flag(0, 0, false) == 0, "an unbound stage needs no sampler flag");
}

void test_light_type() {
    check(render::light_uses_position(1), "a point light uses its position");
    check(render::light_uses_position(2), "a spot light uses its position");
    check(!render::light_uses_position(3), "a directional light uses its direction");
}

void test_pso_key() {
    unsigned rs[256] = {};
    rs[render::kRsZFunc] = 4;
    unsigned long long base = render::pso_key(false, rs);
    check(base == render::pso_key(false, rs), "pso key is stable for one state");

    rs[render::kRsClipping] = 1;
    unsigned long long clipped = render::pso_key(false, rs);
    rs[render::kRsClipping] = 0;
    check(clipped != render::pso_key(false, rs), "d3d8 clipping changes the pipeline key");
    check(render::pso_key(true, rs) != render::pso_key(false, rs),
          "the render target format changes the pipeline key");

    unsigned z[256] = {};
    z[render::kRsZEnable] = 1;
    check(render::pso_key(false, z) != render::pso_key(false, rs), "z test changes the key");
}

void test_primitive_counts() {
    check(render::primitive_vertices(4, 2) == 6, "a 2-triangle list is 6 vertices");
    check(render::primitive_vertices(5, 2) == 4, "a 2-triangle strip is 4 vertices");
    check(render::primitive_vertices(6, 2) == 4, "a 2-triangle fan is 4 vertices");
    check(render::primitive_vertices(5, 0) == 0, "an empty strip has no vertices");
    check(render::primitive_vertices(2, 4) == 0, "a line list is not a triangle");
}

void test_strip_and_fan() {
    unsigned out[8];
    const unsigned strip[4] = {0, 1, 2, 3};
    const unsigned stripWant[6] = {0, 1, 2, 2, 1, 3};
    std::memset(out, 0, sizeof out);
    unsigned count = expand_count(5, strip, 4, out, 6);
    check_indices(out, count, stripWant, 6, "a 4-vertex strip is two triangles");

    const unsigned fan[4] = {0, 1, 2, 3};
    const unsigned fanWant[6] = {0, 1, 2, 0, 2, 3};
    std::memset(out, 0, sizeof out);
    count = expand_count(6, fan, 4, out, 6);
    check_indices(out, count, fanWant, 6, "a 4-vertex fan is two triangles");

    const unsigned indexedStrip[4] = {10, 11, 12, 13};
    const unsigned indexedStripWant[6] = {10, 11, 12, 12, 11, 13};
    std::memset(out, 0, sizeof out);
    count = expand_count(5, indexedStrip, 4, out, 6);
    check_indices(out, count, indexedStripWant, 6, "an indexed strip keeps its indices and winding");

    const unsigned indexedFan[4] = {7, 8, 9, 10};
    const unsigned indexedFanWant[6] = {7, 8, 9, 7, 9, 10};
    std::memset(out, 0, sizeof out);
    count = expand_count(6, indexedFan, 4, out, 6);
    check_indices(out, count, indexedFanWant, 6, "an indexed fan keeps its indices and winding");

    const unsigned list[6] = {4, 5, 6, 7, 8, 9};
    std::memset(out, 0, sizeof out);
    check(expand_count(4, list, 6, out, 6) == 6, "a 6-vertex list stays a 6-index list");
    check_indices(out, 6, list, 6, "a list is copied unchanged");

    unsigned small[6] = {0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu,
                         0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu};
    unsigned refused = expand_count(5, strip, 4, small, 4);
    check(refused == 0, "a block too small for the expansion is refused");
    bool untouched = true;
    for (unsigned i = 0; i < 6; ++i) untouched = untouched && small[i] == 0xDEADBEEFu;
    check(untouched, "a refused expansion writes nothing");
}

void test_expansion_block() {
    const unsigned strip[4] = {0, 1, 2, 3};
    const unsigned sentinel = 0xDEADBEEFu;
    unsigned block[6] = {sentinel, sentinel, sentinel, sentinel, sentinel, sentinel};
    unsigned count = expand_count(5, strip, 4, block, 4);
    check(count == 0, "a 4-index block is refused for a 4-vertex strip");
    bool bounded = block[0] == sentinel && block[1] == sentinel && block[2] == sentinel &&
                   block[3] == sentinel && block[4] == sentinel && block[5] == sentinel;
    check(bounded, "the pre-repair expansion wrote past its block");
}

void test_alpha_compare() {
    struct Case {
        unsigned func, alpha8, ref8;
        bool keep;
        const char* what;
    } cases[] = {
        {1, 0, 0, false, "D3DCMP_NEVER discards at alpha 0 and ref 0"},
        {1, 255, 128, false, "D3DCMP_NEVER discards even at alpha 255"},
        {2, 100, 128, true, "D3DCMP_LESS keeps an alpha under the ref"},
        {2, 128, 128, false, "D3DCMP_LESS discards at the ref"},
        {3, 128, 128, true, "D3DCMP_EQUAL keeps alpha at the ref"},
        {3, 127, 128, false, "D3DCMP_EQUAL discards alpha off the ref"},
        {4, 128, 128, true, "D3DCMP_LESSEQUAL keeps at the ref"},
        {4, 129, 128, false, "D3DCMP_LESSEQUAL discards above the ref"},
        {5, 129, 128, true, "D3DCMP_GREATER keeps above the ref"},
        {5, 128, 128, false, "D3DCMP_GREATER discards at the ref"},
        {6, 127, 128, true, "D3DCMP_NOTEQUAL keeps off the ref"},
        {6, 128, 128, false, "D3DCMP_NOTEQUAL discards at the ref"},
        {7, 128, 128, true, "D3DCMP_GREATEREQUAL keeps at the ref"},
        {7, 127, 128, false, "D3DCMP_GREATEREQUAL discards under the ref"},
        {8, 0, 128, true, "D3DCMP_ALWAYS keeps alpha 0"},
        {8, 255, 128, true, "D3DCMP_ALWAYS keeps alpha 255"},
        {0, 0, 255, true, "an unset ALPHAFUNC keeps the pixel (d3d8's default is ALWAYS)"},
    };
    for (const Case& c : cases) {
        check(rules::alpha_pass(c.func, c.alpha8, c.ref8) == c.keep, c.what);
    }
}

void test_stage_cascade() {
    check(rules::effective_stages(1, 5) == 0,
          "a disabled stage 0 stops an enabled stage 1: the cascade serves none");
    check(rules::effective_stages(1, 1) == 0, "two disabled stages serve none");
    check(rules::effective_stages(4, 1) == 1, "a disabled stage 1 leaves one stage");
    check(rules::effective_stages(4, 5) == 2, "two enabled stages serve two");
}

void test_fog_modes() {
    check(rules::fog_mode_linear(3), "D3DFOG_LINEAR is the mode this backend models");
    check(rules::fog_mode_linear(0), "D3DFOG_NONE is off, not an unsupported mode");
    check(!rules::fog_mode_linear(1), "D3DFOG_EXP is not fogged as linear");
    check(!rules::fog_mode_linear(2), "D3DFOG_EXP2 is not fogged as linear");
}

void test_fog_applies() {
    check(rules::fog_applies(true, 3, 0), "D3DFOG_LINEAR table fog applies the linear factor");
    check(rules::fog_applies(true, 0, 3), "linear vertex fog applies when the table mode is NONE");
    check(!rules::fog_applies(false, 3, 0), "fog disabled applies nothing");
    check(!rules::fog_applies(true, 0, 0), "an enabled fog with no mode applies nothing");
    check(!rules::fog_applies(true, 1, 0), "D3DFOG_EXP table fog is not applied as a linear fog");
    check(!rules::fog_applies(true, 0, 2), "D3DFOG_EXP2 vertex fog is not applied as a linear fog");
    check(rules::fog_applies(true, 3, 1), "linear table fog applies beside an EXP vertex mode");
    check(!rules::fog_applies(true, 1, 3), "EXP table fog wins over a linear vertex mode");
}

void test_blend_ops() {
    check(rules::blend_op_index(1) == 0, "D3DBLENDOP_ADD is the backend's ADD");
    check(rules::blend_op_index(2) == 1, "D3DBLENDOP_SUBTRACT maps to subtract");
    check(rules::blend_op_index(3) == 2, "D3DBLENDOP_REVSUBTRACT maps to reverse subtract");
    check(rules::blend_op_index(4) == 3, "D3DBLENDOP_MIN reaches the alpha channel as MIN");
    check(rules::blend_op_index(5) == 4, "D3DBLENDOP_MAX reaches the alpha channel as MAX");
    check(rules::blend_op_index(0) == 0, "an unset BLENDOP is ADD");
}

void test_texcoord_index() {
    check(render::texcoord_set(0x00000000u) == 0, "tci 0 reads coordinate set 0");
    check(render::texcoord_source(0x00000000u) == 0, "tci 0 is passthru");
    check(render::texcoord_set(0x00000001u) == 1, "tci 1 reads coordinate set 1");
    check(render::texcoord_source(0x00000001u) == 0, "tci 1 is still passthru");
    check(render::texcoord_set(0x00020002u) == 2, "tci 0x20002 keeps its set index");
    check(render::texcoord_source(0x00020002u) == 2, "tci 0x20002 is camera-space position");
    check(render::texcoord_set(0x00030000u) == 0, "tci 0x30000 reads set 0");
    check(render::texcoord_source(0x00030000u) == 3, "tci 0x30000 is camera-space reflection");
    check(render::texcoord_source(0x00010000u) == 1, "tci 0x10000 is camera-space normal");
}

void test_arguments() {
    check(render::arg_selector(2) == 2, "TEXTURE selects by its low nibble");
    check(!render::arg_modified(4), "a bare SPECULAR argument carries no modifier");
    check(render::arg_selector(0x12) == 2 && render::arg_complement(0x12),
          "0x12 is TEXTURE with D3DTA_COMPLEMENT");
    check(render::arg_selector(0x22) == 2 && render::arg_alpha_replicate(0x22),
          "0x22 is TEXTURE with D3DTA_ALPHAREPLICATE");
    check(render::arg_complement(0x32) && render::arg_alpha_replicate(0x32),
          "0x32 carries both read modifiers");
}

void test_fog() {

    check(render::fog_amount(true, 170.0f, 350.0f, 0.9f) == 1.0f,
          "a depth under FogStart is no fog");
    check(render::fog_amount(true, 170.0f, 350.0f, 260.0f) == 0.5f, "the middle of the range is 0.5");
    check(render::fog_amount(true, 170.0f, 350.0f, 350.0f) == 0.0f, "FogEnd is fully fogged");
    check(render::fog_amount(false, 170.0f, 350.0f, 260.0f) == 1.0f, "disabled fog is no fog");
    check(render::fog_amount(true, 350.0f, 170.0f, 260.0f) == 1.0f,
          "a non-increasing range is no fog");
}

void test_descriptor_partition() {
    check(render::pair_base(0) == 1, "descriptor 0 stays the white texel");
    check(render::pair_capacity() == 4095, "the pair region holds 4,095 pairs");
    const unsigned last = render::pair_capacity() - 1;
    check(render::pair_base(last) == 8189, "the last pair starts at descriptor 8,189");
    check(render::pair_base(last) + 1 == 8190, "the last pair ends at descriptor 8,190");
    check(render::kDescriptors == 16384, "the heap is 16,384 descriptors");
    check(render::kPairTop == 8192, "the texture region starts at 8,192, leaving 8,191 unused");
    check(render::kDescriptors == render::kPairTop + render::texture_capacity(),
          "the two regions cover the heap exactly");
    check(render::texture_capacity() == 8192, "8,192 texture slots");

    unsigned used = 0, restarts = 0;
    bool crossed = false;
    for (unsigned draw = 0; draw < 8200; ++draw) {
        if (used == render::pair_capacity()) {
            ++restarts;
            used = 0;
        }
        unsigned base = render::pair_base(used++);
        crossed = crossed || base + 1 >= render::kPairTop;
    }
    check(!crossed, "no draw's pair crosses into the texture region");
    check(restarts == 2, "8,200 draws restart the list twice: a list holds 4,095 draws");
    check(used == 8200 - 2 * 4095, "the restarted list holds the draws after the last restart");
}

void test_srv_slots() {
#ifdef TJ_RED_HISTORY
    red::Device12 slots;
#else
    render::SrvSlots slots;
#endif
    static unsigned seen[render::kDescriptors] = {};
    std::memset(seen, 0, sizeof seen);
    unsigned repeated = 0, outside = 0;
    unsigned first = slots.alloc();
    if (first < render::kPairTop || first >= render::kDescriptors) ++outside;
    ++seen[first % render::kDescriptors];
    for (unsigned i = 1; i < render::texture_capacity(); ++i) {
        unsigned slot = slots.alloc();
        if (slot < render::kPairTop || slot >= render::kDescriptors) ++outside;
        if (seen[slot % render::kDescriptors]++) ++repeated;
    }
    check(outside == 0, "every texture slot is inside the texture region");
    check(repeated == 0, "8,192 live textures hold 8,192 distinct descriptors");
    unsigned next = slots.alloc();
    check(next == 0, "an exhausted allocator returns the white texel, not another texture's slot");
    check(slots.exhausted >= 1, "exhaustion is counted");
    slots.release(first);
    unsigned comeback = slots.alloc();
    check(slots.reused >= 1, "a released slot is counted as reused");
    check(comeback == first, "the released slot is the next one handed out");
}

}

void test_retire_fence_rule() {
    rules::RetireQueue q;
    static rules::RetireQueue::Entry ready[rules::RetireQueue::kSlots];
    const unsigned slots = rules::RetireQueue::kSlots;

    unsigned dropped = q.push(true, 7, 0xA000, 8192);
    check(dropped == 0, "a release during recording is held, not dropped");
    check(q.pendingCount == 1 && q.count == 0, "a release during recording waits for the list");

    check(q.drain(8, ready, slots) == 0, "an upload fence does not release what the open list reads");
    check(q.count == 0 && q.pendingCount == 1, "the entry keeps waiting after the upload");

    check(q.bind(9) == 0, "the closing submit takes the pending entries");
    check(q.pendingCount == 0 && q.count == 1, "the closing submit owns the entry");
    check(q.drain(8, ready, slots) == 0, "an earlier value does not free it");
    unsigned n = q.drain(9, ready, slots);
    check(n == 1 && ready[0].resource == 0xA000 && ready[0].srv == 8192,
          "the list's own fence frees the resource and its descriptor slot");
    check(q.count == 0, "the table is empty after the drain");
}

void test_retire_bound() {
    rules::RetireQueue q;
    static rules::RetireQueue::Entry ready[rules::RetireQueue::kSlots];
    const unsigned slots = rules::RetireQueue::kSlots;
    unsigned stored = 0, dropped = 0;
    for (unsigned i = 0; i <= slots; ++i) {
        if (q.push(false, 0, 0x1000 + i, 8192 + i)) {
            ++dropped;
        } else {
            ++stored;
        }
    }
    check(stored == slots, "the table holds exactly its bound");
    check(dropped == 1 && q.dropped == 1, "the entry past the bound is dropped and counted");
    check(q.drain(1, ready, slots) == slots, "one completed submit frees every entry it holds");
    check(q.count == 0, "the drained table is empty");

    rules::RetireQueue open;
    unsigned kept = 0;
    for (unsigned i = 0; i < slots + 1; ++i) kept += open.push(true, 0, 0x2000 + i, 8192 + i) == 0;
    check(open.pendingCount == slots && kept == slots, "the pending bucket holds its bound");
    check(open.dropped == 1, "a full pending bucket drops and counts");
    check(open.drain(64, ready, slots) == 0, "a wait does not drain the pending bucket");
}

void test_pending_bucket_needs_a_completed_wait() {
    rules::RetireQueue::Entry ready[rules::RetireQueue::kSlots] = {};

    rules::RetireQueue kept;
    kept.push(true, 7, 0xB000, 9000);
    check(kept.pendingCount == 1, "a release during recording waits in the pending bucket");
    bool transferPending = false;
    check(!rules::RetireQueue::release_pending_on_complete(false, transferPending),
          "a failed wait keeps the bucket with no transfer in flight");
    check(!transferPending, "a failed wait with no transfer leaves the flag clear");
    check(
        kept.drain_pending(rules::RetireQueue::release_pending_on_complete(false, transferPending),
                           ready, rules::RetireQueue::kSlots) == 0,
        "a pending entry survives a failed wait even with no transfer in flight");
    check(kept.pendingCount == 1, "the kept entry keeps its descriptor slot");
    transferPending = true;
    check(!rules::RetireQueue::release_pending_on_complete(false, transferPending),
          "a failed wait beside a timed-out transfer keeps the bucket");
    check(transferPending, "a failed wait leaves the transfer flag set");

    rules::RetireQueue released;
    released.push(true, 7, 0xB000, 9000);
    bool release = rules::RetireQueue::release_pending_on_complete(true, transferPending);
    check(release && !transferPending,
          "a completed wait clears the transfer flag and releases the bucket");
    unsigned n = released.drain_pending(release, ready, rules::RetireQueue::kSlots);
    check(n == 1 && ready[0].resource == 0xB000 && ready[0].srv == 9000,
          "a completed wait releases the pending resource and its slot");
    check(released.pendingCount == 0, "the released pending bucket is empty");
}

void test_submit_binds_the_signalled_fence() {
    rules::RetireQueue q;
    rules::RetireQueue::Entry ready[rules::RetireQueue::kSlots] = {};
    const unsigned long long lastSignalled = 41;
    q.push(true, lastSignalled, 0xC000, 9001);
    check(q.bind(lastSignalled + 1) == 0, "the closing submit takes the pending entry");
    check(q.drain(lastSignalled, ready, rules::RetireQueue::kSlots) == 0,
          "the previous signalled value frees nothing");
    unsigned n = q.drain(lastSignalled + 1, ready, rules::RetireQueue::kSlots);
    check(n == 1 && ready[0].srv == 9001 && ready[0].resource == 0xC000,
          "the value the submission signals frees the pending resource");
}

void test_thread_guard_refuses_a_second_thread() {
    rules::ThreadGuard guard;
    bool first = false;
    guard.own(1);
    check(guard.enter(1, first), "the owning thread enters the guard");
    check(!first && guard.count() == 0, "the owner's call is not counted as a violation");
    bool refused = false;
    std::thread other([&] {
        bool logged = false;
        refused = !guard.enter(2, logged);
        first = logged;
    });
    other.join();
    check(refused, "a second thread is refused");
    check(first, "the first refusal asks for one log row");
    check(guard.count() == 1, "the refusal is counted once");
    check(guard.frame_failed(), "a refusal fails the frame");
    bool again = false;
    check(!guard.enter(2, again) && !again, "a second refusal is counted without another row");
    check(guard.count() == 2, "every refusal is counted");
}

void test_index_guard() {
    check(!rules::index_overruns(0, 50, 100, 101), "a 16-bit buffer holds the count it sizes");
    check(rules::index_overruns(0, 51, 100, 101), "one index past a 16-bit buffer is refused");
    check(!rules::index_overruns(20, 5, 100, 102), "a 32-bit buffer holds 25 indices");
    check(rules::index_overruns(20, 6, 100, 102), "one index past a 32-bit buffer is refused");
    check(rules::index_overruns(0, 1, 0, 101), "an empty index buffer overruns");
}

void test_index_clamp() {
    const unsigned room = 50;
    const size_t page = 4096;
    unsigned char* pages = (unsigned char*)VirtualAlloc(nullptr, page * 2, MEM_COMMIT | MEM_RESERVE,
                                                        PAGE_READWRITE);
    check(pages != nullptr, "the clamp test reserves its pages");
    if (!pages) return;
    DWORD old = 0;
    VirtualProtect(pages + page, page, PAGE_NOACCESS, &old);
    unsigned* indices = (unsigned*)(pages + page - (size_t)room * sizeof(unsigned));
    for (unsigned i = 0; i < room; ++i) indices[i] = i;
    const unsigned length = room * sizeof(unsigned);
    unsigned vertices = rules::clamp_index_count(0, room + 1, length, 102);
    check(vertices == room, "an over-long index count is clamped to the index buffer");
    if (vertices <= room) {
        unsigned out[room + 8] = {};
        unsigned count = render::expand_indices(4, indices, vertices, out, room + 8);
        bool kept = count == room;
        for (unsigned i = 0; i < count && kept; ++i) kept = out[i] == i;
        check(kept, "the clamped count expands inside the index buffer");
    }
    check(rules::clamp_index_count(room, 3, length, 102) == 0,
          "a start index at the end of the index buffer is rejected");
    check(rules::clamp_index_count(room + 1, 3, length, 102) == 0,
          "a start index past the index buffer is rejected");
    VirtualFree(pages, 0, MEM_RELEASE);
}

int main() {
    test_state_map();
    test_mip_filter();
    test_light_type();
    test_pso_key();
    test_primitive_counts();
    test_strip_and_fan();
    test_expansion_block();
    test_alpha_compare();
    test_stage_cascade();
    test_fog_modes();
    test_fog_applies();
    test_blend_ops();
    test_texcoord_index();
    test_arguments();
    test_fog();
    test_descriptor_partition();
    test_srv_slots();
    test_retire_fence_rule();
    test_retire_bound();
    test_pending_bucket_needs_a_completed_wait();
    test_submit_binds_the_signalled_fence();
    test_thread_guard_refuses_a_second_thread();
    test_index_guard();
    test_index_clamp();
#ifdef TJ_RED_HISTORY
    std::printf("pre-repair: %d checks, %d failures\n", checks, failures);
#else
    std::printf("fixed: %d checks, %d failures\n", checks, failures);
#endif
    return failures ? 1 : 0;
}
