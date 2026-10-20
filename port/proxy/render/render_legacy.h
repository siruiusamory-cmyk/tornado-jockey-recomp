
#pragma once

#include "render.h"

namespace render {

template <class R>
R forward0(void* real, void** vtbl, unsigned slot) {
    typedef R(__stdcall * Fn)(void*);
    return reinterpret_cast<Fn>(vtbl[slot])(real);
}

template <class R, class... A>
R forward(void* real, void** vtbl, unsigned slot, A... args) {
    typedef R(__stdcall * Fn)(void*, A...);
    return reinterpret_cast<Fn>(vtbl[slot])(real, args...);
}

struct LegacyBase {
    void* com;
    void** vtbl;

    LegacyBase(void* object, void** table) : com(object), vtbl(table) {}
    explicit LegacyBase(void* object) : com(object), vtbl(*static_cast<void***>(object)) {}

    template <class R>
    R call0(unsigned slot) const {
        return forward0<R>(com, vtbl, slot);
    }
    template <class R, class... A>
    R call(unsigned slot, A... args) const {
        return forward<R>(com, vtbl, slot, args...);
    }
};

struct LegacySurface : Surface, LegacyBase {
    explicit LegacySurface(void* object) : LegacyBase(object) {}
    void* raw() const override { return com; }
    ULONG AddRef() override { return call0<ULONG>(1); }
    ULONG Release() override { return call0<ULONG>(2); }
    HRESULT GetDevice(void** out) override { return call<HRESULT>(3, out); }
    HRESULT GetDesc(void* desc) override { return call<HRESULT>(8, desc); }
    HRESULT LockRect(void* locked, const RECT* rect, DWORD flags) override {
        return call<HRESULT>(9, locked, rect, flags);
    }
    HRESULT UnlockRect() override { return call0<HRESULT>(10); }
};

struct LegacyTexture : Texture, LegacyBase {
    bool cube;
    explicit LegacyTexture(void* object, bool is_cube = false) : LegacyBase(object), cube(is_cube) {}
    void* raw() const override { return com; }
    ULONG AddRef() override { return call0<ULONG>(1); }
    ULONG Release() override { return call0<ULONG>(2); }
    HRESULT GetDevice(void** out) override { return call<HRESULT>(3, out); }
    DWORD GetLevelCount() override { return call0<DWORD>(13); }
    HRESULT GetLevelDesc(UINT level, void* desc) override {
        return call<HRESULT>(14, level, desc);
    }
    HRESULT GetSurface(UINT face, UINT level, Surface** out) override {
        void* object = nullptr;
        HRESULT hr = cube ? call<HRESULT>(15, face, level, &object) : call<HRESULT>(15, level, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacySurface(object);
        return hr;
    }
    HRESULT LockRect(UINT level, void* locked, const RECT* rect, DWORD flags) override {
        return call<HRESULT>(16, level, locked, rect, flags);
    }
    HRESULT UnlockRect(UINT level) override { return call<HRESULT>(17, level); }
    void PreLoad() override { call0<void>(9); }
};

struct LegacyBuffer : Buffer, LegacyBase {
    explicit LegacyBuffer(void* object) : LegacyBase(object) {}
    void* raw() const override { return com; }
    ULONG AddRef() override { return call0<ULONG>(1); }
    ULONG Release() override { return call0<ULONG>(2); }
    HRESULT GetDevice(void** out) override { return call<HRESULT>(3, out); }
    HRESULT Lock(UINT offset, UINT size, unsigned char** bits, DWORD flags) override {
        return call<HRESULT>(11, offset, size, bits, flags);
    }
    HRESULT Unlock() override { return call0<HRESULT>(12); }
    HRESULT GetDesc(void* desc) override { return call<HRESULT>(13, desc); }
};

struct Legacy : Device {
    void* real;
    void** vtbl;

    explicit Legacy(void* device) : real(device), vtbl(*static_cast<void***>(device)) {}

    template <class R>
    R call0(unsigned slot) const {
        return forward0<R>(real, vtbl, slot);
    }
    template <class R, class... A>
    R call(unsigned slot, A... args) const {
        return forward<R>(real, vtbl, slot, args...);
    }

    ULONG AddRef() override { return call0<ULONG>(1); }
    ULONG Release() override { return call0<ULONG>(2); }
    HRESULT BeginScene() override { return call0<HRESULT>(34); }
    HRESULT EndScene() override { return call0<HRESULT>(35); }
    HRESULT BeginStateBlock() override { return call0<HRESULT>(52); }
    UINT GetAvailableTextureMem() override { return call0<UINT>(4); }

#define RENDER_ARGS(...) __VA_ARGS__
#define RENDER_FORWARD(ret, name, slot, params, args)                                          \
    ret name params override {                                                                 \
        if (skip_draw(slot)) return ret();                                                     \
        return call<ret>(slot, RENDER_ARGS args);                                              \
    }
    RENDER_DEVICE_CALLS(RENDER_FORWARD)
#undef RENDER_FORWARD

    HRESULT GetBackBuffer(UINT index, unsigned type, Surface** out) override {
        void* object = nullptr;
        HRESULT hr = call<HRESULT>(16, index, type, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacySurface(object);
        return hr;
    }
    HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, DWORD format,
                          DWORD pool, Texture** out) override {
        void* object = nullptr;
        HRESULT hr = call<HRESULT>(20, width, height, levels, usage, format, pool, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacyTexture(object);
        return hr;
    }
    HRESULT CreateCubeTexture(UINT edge, UINT levels, DWORD usage, DWORD format, DWORD pool,
                              Texture** out) override {
        void* object = nullptr;
        HRESULT hr = call<HRESULT>(22, edge, levels, usage, format, pool, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacyTexture(object, true);
        return hr;
    }
    HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, DWORD pool,
                               Buffer** out) override {
        void* object = nullptr;
        HRESULT hr = call<HRESULT>(23, length, usage, fvf, pool, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacyBuffer(object);
        return hr;
    }
    HRESULT CreateIndexBuffer(UINT length, DWORD usage, DWORD format, DWORD pool,
                              Buffer** out) override {
        void* object = nullptr;
        HRESULT hr = call<HRESULT>(24, length, usage, format, pool, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacyBuffer(object);
        return hr;
    }
    HRESULT CreateDepthStencilSurface(UINT width, UINT height, DWORD format,
                                      unsigned multisample, Surface** out) override {
        void* object = nullptr;
        HRESULT hr = call<HRESULT>(26, width, height, format, multisample, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacySurface(object);
        return hr;
    }
    HRESULT CreateImageSurface(UINT width, UINT height, DWORD format, Surface** out) override {
        void* object = nullptr;
        HRESULT hr = call<HRESULT>(27, width, height, format, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacySurface(object);
        return hr;
    }
    HRESULT CopyRects(Surface* source, const RECT* sourceRects, UINT count, Surface* dest,
                      const POINT* destPoints) override {
        return call<HRESULT>(28, source ? source->raw() : nullptr, sourceRects, count,
                             dest ? dest->raw() : nullptr, destPoints);
    }
    HRESULT UpdateTexture(Texture* source, Texture* dest) override {
        return call<HRESULT>(29, source ? source->raw() : nullptr, dest ? dest->raw() : nullptr);
    }
    HRESULT SetRenderTarget(Surface* target, Surface* depth) override {
        return call<HRESULT>(31, target ? target->raw() : nullptr, depth ? depth->raw() : nullptr);
    }
    HRESULT GetRenderTarget(Surface** out) override {
        void* object = nullptr;
        HRESULT hr = call<HRESULT>(32, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacySurface(object);
        return hr;
    }
    HRESULT GetDepthStencilSurface(Surface** out) override {
        void* object = nullptr;
        HRESULT hr = call<HRESULT>(33, &object);
        if (SUCCEEDED(hr) && object) *out = new LegacySurface(object);
        return hr;
    }
    HRESULT SetTexture(DWORD stage, Texture* texture) override {
        return call<HRESULT>(61, stage, texture ? texture->raw() : nullptr);
    }
    HRESULT SetStreamSource(UINT stream, Buffer* buffer, UINT stride) override {
        return call<HRESULT>(83, stream, buffer ? buffer->raw() : nullptr, stride);
    }
    HRESULT SetIndices(Buffer* buffer, UINT baseVertex) override {
        return call<HRESULT>(85, buffer ? buffer->raw() : nullptr, baseVertex);
    }
#undef RENDER_ARGS
};

inline Legacy* attach(void* real) {
    Legacy* backend = new Legacy(real);
    set_device(backend);
    backend_name_slot() = "legacy";
    return backend;
}

}
