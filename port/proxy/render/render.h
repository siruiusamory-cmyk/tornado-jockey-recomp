
#pragma once

#include <windows.h>

#include <cstdint>

namespace render {

struct Surface;

struct Resource {
    virtual ~Resource() {}
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
    virtual HRESULT GetDevice(void** out) = 0;
    virtual void* raw() const = 0;
};

struct Texture : Resource {
    virtual DWORD GetLevelCount() = 0;
    virtual HRESULT GetLevelDesc(UINT level, void* desc) = 0;

    virtual HRESULT GetSurface(UINT face, UINT level, Surface** out) = 0;
    virtual HRESULT LockRect(UINT level, void* locked, const RECT* rect, DWORD flags) = 0;
    virtual HRESULT UnlockRect(UINT level) = 0;
    virtual void PreLoad() = 0;
};

struct Surface : Resource {
    virtual HRESULT GetDesc(void* desc) = 0;
    virtual HRESULT LockRect(void* locked, const RECT* rect, DWORD flags) = 0;
    virtual HRESULT UnlockRect() = 0;
};

struct Buffer : Resource {
    virtual HRESULT Lock(UINT offset, UINT size, unsigned char** bits, DWORD flags) = 0;
    virtual HRESULT Unlock() = 0;
    virtual HRESULT GetDesc(void* desc) = 0;
};

#define RENDER_DEVICE_CALLS(X)                                                                  \
    X(HRESULT, GetDirect3D, 6, (void** out), (out))                                             \
    X(HRESULT, GetDeviceCaps, 7, (void* caps), (caps))                                          \
    X(HRESULT, GetDisplayMode, 8, (void* mode), (mode))                                         \
    X(HRESULT, ShowCursor, 12, (BOOL show), (show))                                             \
    X(HRESULT, Reset, 14, (void* params), (params))                                             \
    X(HRESULT, Present, 15,                                                                    \
      (const RECT* source, const RECT* dest, HWND window, const void* dirty),                   \
      (source, dest, window, dirty))                                                            \
    X(HRESULT, Clear, 36,                                                                      \
      (DWORD rectCount, const RECT* rects, DWORD flags, DWORD color, float z, DWORD stencil),   \
      (rectCount, rects, flags, color, z, stencil))                                             \
    X(HRESULT, SetTransform, 37, (unsigned state, const void* matrix), (state, matrix))         \
    X(HRESULT, GetTransform, 38, (unsigned state, void* matrix), (state, matrix))               \
    X(HRESULT, SetViewport, 40, (const void* viewport), (viewport))                             \
    X(HRESULT, GetViewport, 41, (void* viewport), (viewport))                                   \
    X(HRESULT, SetMaterial, 42, (const void* material), (material))                             \
    X(HRESULT, SetLight, 44, (DWORD index, const void* light), (index, light))                  \
    X(HRESULT, LightEnable, 46, (DWORD index, BOOL enable), (index, enable))                    \
    X(HRESULT, SetRenderState, 50, (unsigned state, DWORD value), (state, value))               \
    X(HRESULT, GetRenderState, 51, (unsigned state, DWORD* value), (state, value))              \
    X(HRESULT, EndStateBlock, 53, (DWORD* token), (token))                                      \
    X(HRESULT, ApplyStateBlock, 54, (DWORD token), (token))                                     \
    X(HRESULT, CaptureStateBlock, 55, (DWORD token), (token))                                   \
    X(HRESULT, DeleteStateBlock, 56, (DWORD token), (token))                                    \
    X(HRESULT, GetTextureStageState, 62, (DWORD stage, unsigned type, DWORD* value),            \
      (stage, type, value))                                                                    \
    X(HRESULT, SetTextureStageState, 63, (DWORD stage, unsigned type, DWORD value),             \
      (stage, type, value))                                                                    \
    X(HRESULT, ValidateDevice, 64, (DWORD* passes), (passes))                                   \
    X(HRESULT, GetInfo, 65, (DWORD id, void* info, DWORD size), (id, info, size))               \
    X(HRESULT, DrawPrimitive, 70, (unsigned primitive, UINT startVertex, UINT count),            \
      (primitive, startVertex, count))                                                         \
    X(HRESULT, DrawIndexedPrimitive, 71,                                                       \
      (unsigned primitive, UINT minIndex, UINT numVertices, UINT startIndex, UINT count),      \
      (primitive, minIndex, numVertices, startIndex, count))                                   \
    X(HRESULT, DrawPrimitiveUP, 72,                                                            \
      (unsigned primitive, UINT count, const void* data, UINT stride),                         \
      (primitive, count, data, stride))                                                        \
    X(HRESULT, SetVertexShader, 76, (DWORD handle), (handle))

#define RENDER_DEVICE_RESOURCES(X)                                                              \
    X(HRESULT, GetBackBuffer, 16, (UINT index, unsigned type, Surface** out), (index, type, out)) \
    X(HRESULT, CreateTexture, 20,                                                              \
      (UINT width, UINT height, UINT levels, DWORD usage, DWORD format, DWORD pool,             \
       Texture** out),                                                                          \
      (width, height, levels, usage, format, pool, out))                                        \
    X(HRESULT, CreateCubeTexture, 22,                                                          \
      (UINT edge, UINT levels, DWORD usage, DWORD format, DWORD pool, Texture** out),            \
      (edge, levels, usage, format, pool, out))                                                 \
    X(HRESULT, CreateVertexBuffer, 23,                                                         \
      (UINT length, DWORD usage, DWORD fvf, DWORD pool, Buffer** out),                          \
      (length, usage, fvf, pool, out))                                                          \
    X(HRESULT, CreateIndexBuffer, 24,                                                          \
      (UINT length, DWORD usage, DWORD format, DWORD pool, Buffer** out),                       \
      (length, usage, format, pool, out))                                                       \
    X(HRESULT, CreateDepthStencilSurface, 26,                                                  \
      (UINT width, UINT height, DWORD format, unsigned multisample, Surface** out),             \
      (width, height, format, multisample, out))                                                \
    X(HRESULT, CreateImageSurface, 27, (UINT width, UINT height, DWORD format, Surface** out),  \
      (width, height, format, out))                                                             \
    X(HRESULT, CopyRects, 28,                                                                  \
      (Surface* source, const RECT* sourceRects, UINT count, Surface* dest,                     \
       const POINT* destPoints),                                                                \
      (source, sourceRects, count, dest, destPoints))                                           \
    X(HRESULT, UpdateTexture, 29, (Texture* source, Texture* dest), (source, dest))             \
    X(HRESULT, SetRenderTarget, 31, (Surface* target, Surface* depth), (target, depth))         \
    X(HRESULT, GetRenderTarget, 32, (Surface** out), (out))                                     \
    X(HRESULT, GetDepthStencilSurface, 33, (Surface** out), (out))                              \
    X(HRESULT, SetTexture, 61, (DWORD stage, Texture* texture), (stage, texture))               \
    X(HRESULT, SetStreamSource, 83, (UINT stream, Buffer* buffer, UINT stride),                \
      (stream, buffer, stride))                                                                \
    X(HRESULT, SetIndices, 85, (Buffer* buffer, UINT baseVertex), (buffer, baseVertex))

struct Device {
    virtual ~Device() {}

    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
    virtual UINT GetAvailableTextureMem() = 0;
    virtual HRESULT BeginScene() = 0;
    virtual HRESULT EndScene() = 0;
    virtual HRESULT BeginStateBlock() = 0;

    virtual void counters(unsigned* out) {
        out[0] = out[1] = out[2] = out[3] = 0;
    }

#define RENDER_DECL(ret, name, slot, params, args) virtual ret name params = 0;
    RENDER_DEVICE_CALLS(RENDER_DECL)
    RENDER_DEVICE_RESOURCES(RENDER_DECL)
#undef RENDER_DECL
};

inline Device*& device_slot() {
    static Device* value = nullptr;
    return value;
}
inline Device* device() { return device_slot(); }
inline void set_device(Device* value) { device_slot() = value; }

inline unsigned& frame_number_slot() {
    static unsigned value = 1;
    return value;
}

inline unsigned& skip_frame_slot() {
    static unsigned value = 0;
    return value;
}

inline unsigned& skip_index_slot() {
    static unsigned value = 0;
    return value;
}

inline unsigned& skip_end_slot() {
    static unsigned value = 0;
    return value;
}

inline unsigned& only_frame_slot() {
    static unsigned value = 0;
    return value;
}

inline unsigned& only_index_slot() {
    static unsigned value = 0;
    return value;
}

inline bool skip_draw(unsigned slot) {
    if (slot != 70 && slot != 71 && slot != 72) return false;
    static unsigned frame = 0;
    static unsigned seen = 0;
    if (frame != frame_number_slot()) {
        frame = frame_number_slot();
        seen = 0;
    }
    ++seen;
    if (frame == only_frame_slot() && only_index_slot()) return seen != only_index_slot();
    if (frame != skip_frame_slot() || !skip_index_slot()) return false;
    return seen >= skip_index_slot() && seen <= (skip_end_slot() ? skip_end_slot() : skip_index_slot());
}

inline void*& device_com_slot() {
    static void* value = nullptr;
    return value;
}
inline void* device_com() { return device_com_slot(); }

inline void*& root_com_slot() {
    static void* value = nullptr;
    return value;
}
inline void* root_com() { return root_com_slot(); }

inline const char*& backend_name_slot() {
    static const char* value = "none";
    return value;
}
inline const char* backend_name() { return backend_name_slot(); }

}
