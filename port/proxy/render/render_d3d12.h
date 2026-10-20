
#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <float.h>

#include "d3d12_shaders.h"
#include "fixed_function.h"
#include "render.h"

namespace render {

constexpr DWORD kFmtA8R8G8B8 = 21;
constexpr DWORD kFmtX8R8G8B8 = 22;
constexpr DWORD kFmtDXT1 = 0x31545844;
constexpr DWORD kFmtDXT3 = 0x33545844;
constexpr DWORD kFmtDXT5 = 0x35545844;
constexpr DWORD kFmtD16 = 80;
constexpr DWORD kFmtD24X8 = 77;
constexpr DWORD kFmtD24S8 = 75;
constexpr DWORD kFmtD32 = 71;

constexpr unsigned kMaxLights = 8;
constexpr unsigned kMaxStages = 8;

constexpr unsigned kVertexStride = 56;

struct Device12;

inline DWORD fvf_position(DWORD fvf) { return fvf & 0x00Eu; }
inline unsigned fvf_stride(DWORD fvf) {
    unsigned size = 0;
    switch (fvf_position(fvf)) {
        case 0x002: size = 12; break;
        case 0x004: size = 16; break;
        default: size = 12; break;
    }
    if (fvf & 0x010) size += 12;
    if (fvf & 0x020) size += 4;
    if (fvf & 0x040) size += 4;
    if (fvf & 0x080) size += 4;
    size += 8 * ((fvf >> 8) & 0xF);
    return size;
}

struct Texture12;

struct IndexTrace {
    const void* list;
    unsigned primitive, vertices, startIndex, count, length, format, reported;
};

inline IndexTrace& index_trace() {
    static IndexTrace trace = {};
    return trace;
}

struct Params {
    unsigned op0, arg00, arg01, aop0;
    unsigned aarg00, aarg01, op1, arg10;
    unsigned arg11, aop1, aarg10, aarg11;
    unsigned flags;
    float alphaRef;
    float fogStart, fogEnd;
    float factor[4];
    float fogColor[4];

    unsigned alphaFunc;
    unsigned stageCount;
};

struct Surface12 : Surface {
    Device12* device;
    Texture12* owner;
    unsigned face = 0, level = 0;
    bool backbuffer = false;
    unsigned rtv = 0;

    unsigned char* pixels;
    unsigned width, height;
    DWORD format;
    ULONG refs = 1;

    ~Surface12();
    ULONG AddRef() override { return ++refs; }
    ULONG Release() override;
    HRESULT GetDevice(void** out) override;
    HRESULT GetDesc(void* desc) override;
    HRESULT LockRect(void* locked, const RECT* rect, DWORD flags) override;
    HRESULT UnlockRect() override;
    void* raw() const override { return nullptr; }
};

struct Texture12 : Texture {
    Device12* device;
    unsigned width, height, levels, faces;
    DWORD usage, format, pool;
    unsigned char* data[6][16];
    ID3D12Resource* gpuRes = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    unsigned srv = 0;
    unsigned rtv = 0;
    bool dirty = true;
    ULONG refs = 1;

    ULONG AddRef() override { return ++refs; }

    ULONG Release() override { return --refs; }
    void* raw() const override { return nullptr; }
    DWORD GetLevelCount() override { return levels; }
    HRESULT GetLevelDesc(UINT level, void* desc) override;
    HRESULT GetSurface(UINT face, UINT level, Surface** out) override;
    HRESULT LockRect(UINT level, void* locked, const RECT* rect, DWORD flags) override;
    HRESULT UnlockRect(UINT level) override;
    void PreLoad() override {}
    HRESULT GetDevice(void** out) override;
    ~Texture12();
};

struct Buffer12 : Buffer {
    unsigned char* data;
    UINT length;
    DWORD usage, fvf, pool, format;
    bool index;
    ULONG refs = 1;

    ULONG AddRef() override { return ++refs; }

    ULONG Release() override { return --refs; }
    void* raw() const override { return nullptr; }
    HRESULT GetDevice(void** out) override;
    HRESULT Lock(UINT offset, UINT size, unsigned char** bits, DWORD flags) override;
    HRESULT Unlock() override { return S_OK; }
    HRESULT GetDesc(void* desc) override;
};

struct Light8 {
    DWORD type;
    float diffuse[4];
    float specular[4];
    float ambient[4];
    float position[3];
    float direction[3];
    float range;
    float falloff;
    float attenuation0, attenuation1, attenuation2;
    float theta, phi;
};

inline void mul4(const float* a, const float* b, float* out) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] +
                             a[i * 4 + 2] * b[2 * 4 + j] + a[i * 4 + 3] * b[3 * 4 + j];
        }
    }
}

struct Device12 : Device {
    static constexpr unsigned kPsoSlots = 64;

    HWND window = nullptr;
    unsigned width = 0, height = 0;
    DWORD backFormat = kFmtX8R8G8B8;
    ULONG refs = 1;
    unsigned caps[52] = {};

    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain3* swap = nullptr;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    ID3D12DescriptorHeap* dsvHeap = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    unsigned rtvSize = 0, srvSize = 0;
    ID3D12RootSignature* root = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12GraphicsCommandList* xfer = nullptr;
    ID3D12CommandAllocator* alloc[2] = {nullptr, nullptr};
    ID3D12CommandAllocator* xferAlloc = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    ID3D12Resource* back[2] = {nullptr, nullptr};
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* upload = nullptr;
    unsigned char* uploadMap = nullptr;
    unsigned uploadUsed = 0;

    unsigned uploadSize = 64u << 20;
    ID3D12Resource* readback = nullptr;
    ID3D12Resource* whiteBuffer = nullptr;
    unsigned char* readbackMap = nullptr;
    unsigned frameIndex = 0;
    bool sceneOpen = false;

    unsigned pairUsed = 0;

    unsigned pairCapacity = pair_capacity();

    SrvSlots srvSlots;
    unsigned nextRtv = 2;
    bool boundRtv = false;
    unsigned draws = 0, skipped = 0, stageOps = 0;
    unsigned uploaded = 0, noTexture = 0;
    unsigned flushes = 0;
    unsigned gpuTimeouts = 0;
    std::atomic<bool> frameFailed{false};

    RetireQueue retireQueue;
    bool listOpen = false;
    bool xferPending = false;

    ThreadGuard threadGuard;

    struct Combo {
        DWORD v[16];
        unsigned draws;
    } combos[24] = {};
    unsigned comboCount = 0;
    unsigned stage1Draws = 0, projectedDraws = 0, fogDraws = 0, factorDraws = 0;
    unsigned mod2xDraws = 0, modAlphaAddDraws = 0, modArgDraws = 0;
    unsigned fogUnsupported = 0;

    unsigned callsPrimitive = 0, callsIndexed = 0, callsUp = 0, drops = 0, presents = 0;
    unsigned binds = 0, dirtyBinds = 0;
    bool noBlend = false;
    bool noCull = false;
    bool forceAlpha = false;
    bool forceTexture = false;
    bool noDepth = false;
    unsigned rowPresent = 0xFFFFFFFFu;
    bool idEnabled = false;
    unsigned* idMap = nullptr;
    float* idDepth = nullptr;
    unsigned idDraw = 0;
    char idOut[MAX_PATH] = {};

    struct Pso {
        uint64_t key;
        ID3D12PipelineState* state;
    } pso[kPsoSlots] = {};
    unsigned psoCount = 0;

    unsigned rs[256] = {};
    DWORD tss[kMaxStages][32] = {};
    float world[256][16] = {};
    float view[16] = {};
    float textureMatrix[8][16] = {};
    float proj[16] = {};
    float material[5][4] = {};
    Light8 lights[kMaxLights] = {};
    bool lightOn[kMaxLights] = {};
    DWORD fvf = 0;
    bool xfDirty = true;
    Texture12* textures[kMaxStages] = {};
    Buffer12* stream = nullptr;
    UINT streamStride = 0;
    Buffer12* indices = nullptr;
    UINT baseVertex = 0;
    float viewport[6] = {};
    Texture12* targetTexture = nullptr;
    Surface12* depthSurface = nullptr;
    bool capsLoaded = false;

    ULONG AddRef() override { return ++refs; }
    ULONG Release() override { return --refs; }

    bool init(HWND hwnd, unsigned w, unsigned h, DWORD format, DWORD behavior,
              const unsigned* capsDwords);
    void shutdown();

    bool wait_gpu(const char* where);

    void retire(ID3D12Resource* resource, unsigned srv);
    void retire_at(ID3D12Resource* resource, unsigned srv, UINT64 fence);

    void submit_pending(UINT64 value);
    void pump_retired(UINT64 completed);

    void drain_retired();

    bool own_thread(const char* where);
    void flush_draws();
    void reset_frame();
    void begin_target();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle(unsigned index);
    D3D12_CPU_DESCRIPTOR_HANDLE srv_handle(unsigned index);
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu(unsigned index);
    unsigned srv_alloc();
    void srv_free(unsigned index);

    UINT GetAvailableTextureMem() override { return 128u << 20; }
    HRESULT BeginScene() override;
    HRESULT EndScene() override { return S_OK; }
    HRESULT BeginStateBlock() override { return S_OK; }
    void counters(unsigned* out) override {
        out[0] = draws;
        out[1] = skipped;
        out[2] = psoCount;
        out[3] = uploaded;
        out[4] = noTexture;
    }
    HRESULT GetDirect3D(void** out) override;
    HRESULT GetDeviceCaps(void* caps) override;
    HRESULT GetDisplayMode(void* mode) override;
    HRESULT ShowCursor(BOOL show) override { (void)show; return S_OK; }
    HRESULT Reset(void* params) override;
    HRESULT Present(const RECT* source, const RECT* dest, HWND w, const void* dirty) override;
    HRESULT Clear(DWORD count, const RECT* rects, DWORD flags, DWORD color, float z,
                  DWORD stencil) override;
    HRESULT SetTransform(unsigned state, const void* matrix) override;
    HRESULT GetTransform(unsigned state, void* matrix) override;
    HRESULT SetViewport(const void* vp) override;
    HRESULT GetViewport(void* vp) override;
    HRESULT SetMaterial(const void* m) override;
    HRESULT SetLight(DWORD index, const void* light) override;
    HRESULT LightEnable(DWORD index, BOOL enable) override;
    HRESULT SetRenderState(unsigned state, DWORD value) override;
    HRESULT GetRenderState(unsigned state, DWORD* value) override;
    HRESULT EndStateBlock(DWORD* token) override;
    HRESULT ApplyStateBlock(DWORD token) override { (void)token; return S_OK; }
    HRESULT CaptureStateBlock(DWORD token) override { (void)token; return S_OK; }
    HRESULT DeleteStateBlock(DWORD token) override { (void)token; return S_OK; }
    HRESULT GetTextureStageState(DWORD stage, unsigned type, DWORD* value) override;
    HRESULT SetTextureStageState(DWORD stage, unsigned type, DWORD value) override;
    HRESULT ValidateDevice(DWORD* passes) override;
    HRESULT GetInfo(DWORD id, void* info, DWORD size) override;
    HRESULT DrawPrimitive(unsigned primitive, UINT start, UINT count) override;
    HRESULT DrawIndexedPrimitive(unsigned primitive, UINT minIndex, UINT numVertices,
                                 UINT startIndex, UINT count) override;
    HRESULT DrawPrimitiveUP(unsigned primitive, UINT count, const void* data,
                            UINT stride) override;
    HRESULT SetVertexShader(DWORD handle) override;
    HRESULT GetBackBuffer(UINT index, unsigned type, Surface** out) override;
    HRESULT CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, DWORD format, DWORD pool,
                          Texture** out) override;
    HRESULT CreateCubeTexture(UINT edge, UINT levels, DWORD usage, DWORD format, DWORD pool,
                              Texture** out) override;
    HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, DWORD pool,
                               Buffer** out) override;
    HRESULT CreateIndexBuffer(UINT length, DWORD usage, DWORD format, DWORD pool,
                              Buffer** out) override;
    HRESULT CreateDepthStencilSurface(UINT w, UINT h, DWORD format, unsigned ms,
                                      Surface** out) override;
    HRESULT CreateImageSurface(UINT w, UINT h, DWORD format, Surface** out) override;
    HRESULT CopyRects(Surface* source, const RECT* sourceRects, UINT count, Surface* dest,
                      const POINT* destPoints) override;
    HRESULT UpdateTexture(Texture* source, Texture* dest) override;
    HRESULT SetRenderTarget(Surface* target, Surface* depthSurface) override;
    HRESULT GetRenderTarget(Surface** out) override;
    HRESULT GetDepthStencilSurface(Surface** out) override;
    HRESULT SetTexture(DWORD stage, Texture* texture) override;
    HRESULT SetStreamSource(UINT streamIndex, Buffer* buffer, UINT stride) override;
    HRESULT SetIndices(Buffer* buffer, UINT base) override;

    void upload_texture(Texture12* texture);
    void bind_texture(Texture12* texture, unsigned stage, unsigned slot);
    ID3D12PipelineState* pipeline();
    void stage_texcoord(unsigned stage, const float* uv, unsigned texCount, const float* camPos,
                        const float* camNormal, float* out);
    void census_combo();
    void transform_vertices(const unsigned char* base, const UINT* indexList, unsigned count,
                            int firstIndex, unsigned char* out);
    void shade(const float* worldPos, const float* normal, const float* vertexDiffuse,
               const float* vertexSpecular, float* out);
    unsigned emit(const unsigned char* base, const UINT* indexList, unsigned count, int firstIndex);
    HRESULT draw_triangles(unsigned primitive, const unsigned char* base, const UINT* indexList,
                           unsigned vertices, int firstIndex, const char* source);
};

inline Surface12::~Surface12() {
    if (owner) owner->Release();
}

inline Texture12::~Texture12() {
    if (device) device->retire(gpuRes, srv);
}
inline ULONG Surface12::Release() {

    return --refs;
}
inline HRESULT Surface12::GetDevice(void** out) {
    *out = device_com();
    return S_OK;
}
inline HRESULT Texture12::GetDevice(void** out) {
    *out = device_com();
    return S_OK;
}
inline HRESULT Buffer12::GetDevice(void** out) {
    *out = device_com();
    return S_OK;
}

inline unsigned row_bytes(DWORD format, unsigned width) {
    switch (format) {
        case kFmtDXT1: return (width + 3) / 4 * 8;
        case kFmtDXT3:
        case kFmtDXT5: return (width + 3) / 4 * 16;
        default: return width * 4;
    }
}
inline unsigned level_rows(DWORD format, unsigned height) {
    switch (format) {
        case kFmtDXT1:
        case kFmtDXT3:
        case kFmtDXT5: return (height + 3) / 4;
        default: return height;
    }
}
inline unsigned level_bytes(DWORD format, unsigned width, unsigned height) {
    return row_bytes(format, width) * level_rows(format, height);
}

}
