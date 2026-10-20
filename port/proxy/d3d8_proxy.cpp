
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <intrin.h>
#include <tlhelp32.h>

#include "backend.h"
#include "bmp.h"
#include "census.h"
#include "config.h"
#include "d3d8_slots.h"
#include "fixed_step.h"
#include "keep_local.h"
#include "probe_list.h"
#include "render/render.h"
#include "render/render_d3d12.h"
#include "render/render_d3d12_impl1.h"
#include "render/render_d3d12_impl2.h"
#include "render/render_d3d12_impl3.h"
#include "render/render_legacy.h"
#include "replay_script.h"
#include "save.h"
#include "steer.h"
#include "watch.h"
#include "stall.h"
#include "window_mode.h"

extern "C" {
int StepDecide(float delta);
void* g_step_entry;
float g_step_delta;
void StepHook(void);
}

namespace {

constexpr unsigned kTable = 1024;
constexpr unsigned kThunk = 96;
constexpr unsigned kIfaceCount = 7;
constexpr unsigned kCreateDevice = 0x3c / 4;
constexpr unsigned kPresent = 15;
constexpr unsigned kBackBuffer = 16;
constexpr unsigned kCreateImageSurface = 27;
constexpr unsigned kCopyRects = 28;

constexpr unsigned kCreateTexture = 20;
constexpr unsigned kCreateVertexBuffer = 23;
constexpr unsigned kCreateIndexBuffer = 24;
constexpr unsigned kClear = 36;
constexpr unsigned kSetTransform = 37;
constexpr unsigned kSetViewport = 40;
constexpr unsigned kSetRenderState = 50;
constexpr unsigned kSetTexture = 61;
constexpr unsigned kSetTextureStageState = 63;
constexpr unsigned kDrawPrimitive = 70;
constexpr unsigned kDrawIndexedPrimitive = 71;
constexpr unsigned kDrawPrimitiveUP = 72;
constexpr unsigned kDrawIndexedPrimitiveUP = 73;
constexpr unsigned kCreateVertexShader = 75;
constexpr unsigned kSetVertexShader = 76;
constexpr unsigned kCreatePixelShader = 87;
constexpr unsigned kSetPixelShader = 88;

constexpr unsigned kSurfaceRelease = 2;
constexpr unsigned kSurfaceGetDesc = 8;
constexpr unsigned kSurfaceLockRect = 9;
constexpr unsigned kSurfaceUnlockRect = 10;

struct Wrapper {
    void** vtbl;
    void* real;
};

HINSTANCE g_module;
FILE* g_log;
CRITICAL_SECTION g_lock;
DWORD g_flushed_at;
uint64_t g_key[kTable];
unsigned g_count[kTable];
unsigned g_flushed[kTable];
unsigned g_dropped;

bool g_capture;
unsigned g_capture_at;
char g_capture_out[MAX_PATH * 2];
unsigned g_presents;
bool g_captured;
void* g_device;
bool g_windowed;
RECT g_client;

bool g_silence;
bool g_silence_started;

bool g_args;
struct ArgSet {
    bool used;
    unsigned iface, slot, count, printed;
    uint32_t v[6];
};
constexpr unsigned kArgTable = 2048;
ArgSet g_arg_table[kArgTable];
DWORD g_args_flushed_at;
bool ARG_SLOT[2][128];

bool g_io;
constexpr unsigned kIoTable = 256;
struct IoRow {
    volatile LONG count;
    volatile unsigned nameid;
    unsigned printed;
    void* site;
};
IoRow g_io_rows[kIoTable];
unsigned g_io_frame;
DWORD g_io_flushed_at;
bool g_io_armed;

enum HrMethod : unsigned {
    kHrCreateDevice,
    kHrSetDataFormat,
    kHrSetCooperativeLevel,
    kHrAcquire,
};
const char* const kHrMethodNames[] = {"CreateDevice", "SetDataFormat", "SetCooperativeLevel",
                                      "Acquire"};
constexpr unsigned kHrTable = 16;
struct HrRow {
    unsigned used;
    unsigned role;
    unsigned method;
    HRESULT hr;
    unsigned count;
    unsigned printed;
};
HrRow g_hr_rows[kHrTable];
unsigned g_hr_dropped;

bool g_replay;
bool g_fixed_cursor;
constexpr unsigned kMaxEvents = 200;
replay::Event g_events[kMaxEvents];
unsigned g_event_count;
unsigned g_next_event;
replay::State g_state;
unsigned g_activates;
HWND g_hwnd;
unsigned g_probe_stage;
unsigned g_probe_frame;

bool g_probe;
probe::Term g_probe_terms[probe::kMaxTerms];
unsigned g_probe_count;
uintptr_t g_probe_base;
uintptr_t g_watch_base;

bool g_watch;
probe::Term g_watch_term;
uint32_t g_watch_address[2];
bool g_watch_armed;
void* g_watch_veh;
CRITICAL_SECTION g_watch_lock;
uint32_t g_watch_last[2];
unsigned g_watch_hits;
unsigned g_watch_scan_at;

replay::Posted g_posted;

bool g_faithful;
long g_steer_x;
long g_steer_y;
bool g_steering;
constexpr unsigned kSteerClickPresents = 6;

windowmode::Kind g_window_kind;
char g_scale[8];
unsigned g_scale_factor;
unsigned g_back_w;
unsigned g_back_h;
bool g_scaled;
bool g_step_off;
unsigned g_plan_w, g_plan_h;
unsigned g_plan_outer_w, g_plan_outer_h;
int g_plan_x, g_plan_y;
DWORD g_plan_style;

bool g_background;
int g_background_x, g_background_y;
void offscreen_point(int* x, int* y);

bool g_activate_trace;
bool g_focus_guard;

bool g_game_dir_self;
char g_game_dir[MAX_PATH];

enum IoName : unsigned {
    kIoTimeGetTime = 1,
    kIoQpc,
    kIoQpf,
    kIoGetTickCount,
    kIoGetLocalTime,
    kIoSystemTimeAsFileTime,
    kIoSleep,
    kIoGetCursorPos,
    kIoSetCursorPos,
    kIoGetForegroundWindow,
    kIoGetFocus,
    kIoIsIconic,
    kIoShowWindow,
    kIoSetForegroundWindow,
    kIoCreateWindowExA,
    kIoSetWindowPos,
    kIoBringWindowToTop,
    kIoPeekMessage,
    kIoCreateFileA,
    kIoCreateFileW,
    kIoGetFileAttributesA,
    kIoDeleteFileA,
    kIoCreateDirectoryA,
    kIoGetPrivateProfileStringA,
    kIoRegOpenKeyA,
    kIoRegOpenKeyExA,
    kIoRegQueryValueExA,
    kIoRegEnumKeyExA,
    kIoRegQueryInfoKeyA,
    kIoCreateFileMappingA,
    kIoCoCreateInstance,
    kIoSetFocus,
    kIoSetWindowLongA,
    kIoSetActiveWindow,
    kIoCreateWindowExW,
    kIoFindWindowA,
    kIoGetMessage = 36,
    kIoDinput8Create = 0x20,
    kIoDinput8 = 0x40,
    kIoDevice = 0x100,
};
constexpr unsigned kIoDevSlots = 64;
constexpr unsigned kIoDinput8Slots = 11;
constexpr unsigned kIoCreateDevice = 3;
constexpr unsigned kIoEnumDevices = 4;
constexpr unsigned kIoGetCapabilities = 3;
constexpr unsigned kIoEnumObjects = 4;
constexpr unsigned kIoSetProperty = 6;
constexpr unsigned kIoRelease = 2;
constexpr unsigned kIoSetDataFormat = 11;
constexpr unsigned kIoGetDeviceState = 9;
constexpr unsigned kIoAcquire = 7;
constexpr unsigned kIoSetCooperativeLevel = 13;
constexpr unsigned kIoUnacquire = 8;
constexpr unsigned kIoPoll = 25;

typedef DWORD(WINAPI* TimeGetTimeFn)();
typedef BOOL(WINAPI* QueryPerformanceCounterFn)(LARGE_INTEGER*);
typedef BOOL(WINAPI* QueryPerformanceFrequencyFn)(LARGE_INTEGER*);
typedef DWORD(WINAPI* GetTickCountFn)();
typedef void(WINAPI* GetLocalTimeFn)(LPSYSTEMTIME);
typedef void(WINAPI* GetSystemTimeAsFileTimeFn)(LPFILETIME);
typedef void(WINAPI* SleepFn)(DWORD);
typedef BOOL(WINAPI* GetCursorPosFn)(LPPOINT);
typedef BOOL(WINAPI* SetCursorPosFn)(int, int);
typedef HWND(WINAPI* GetForegroundWindowFn)();
typedef HWND(WINAPI* FindWindowAFn)(LPCSTR, LPCSTR);
typedef HWND(WINAPI* GetFocusFn)();
typedef BOOL(WINAPI* IsIconicFn)(HWND);
typedef BOOL(WINAPI* ShowWindowFn)(HWND, int);
typedef BOOL(WINAPI* SetForegroundWindowFn)(HWND);
typedef BOOL(WINAPI* SetWindowPosFn)(HWND, HWND, int, int, int, int, UINT);
typedef BOOL(WINAPI* BringWindowToTopFn)(HWND);
typedef HWND(WINAPI* CreateWindowExAFn)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND,
                                        HMENU, HINSTANCE, LPVOID);
typedef HWND(WINAPI* CreateWindowExWFn)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND,
                                        HMENU, HINSTANCE, LPVOID);
typedef BOOL(WINAPI* PeekMessageFn)(LPMSG, HWND, UINT, UINT, UINT);
typedef BOOL(WINAPI* GetMessageAFn)(LPMSG, HWND, UINT, UINT);
typedef HRESULT(WINAPI* DirectInput8CreateFn)(HINSTANCE, DWORD, const void*, void**, void*);
typedef HANDLE(WINAPI* CreateFileAFn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef HANDLE(WINAPI* CreateFileWFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef DWORD(WINAPI* GetFileAttributesAFn)(LPCSTR);
typedef BOOL(WINAPI* DeleteFileAFn)(LPCSTR);
typedef BOOL(WINAPI* CreateDirectoryAFn)(LPCSTR, LPSECURITY_ATTRIBUTES);
typedef DWORD(WINAPI* GetPrivateProfileStringAFn)(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
typedef LONG(WINAPI* RegOpenKeyAFn)(HKEY, LPCSTR, PHKEY);
typedef LONG(WINAPI* RegOpenKeyExAFn)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LONG(WINAPI* RegQueryValueExAFn)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG(WINAPI* RegEnumKeyExAFn)(HKEY, DWORD, LPSTR, LPDWORD, LPDWORD, LPSTR, LPDWORD,
                                      PFILETIME);
typedef LONG(WINAPI* RegQueryInfoKeyAFn)(HKEY, LPSTR, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD,
                                         LPDWORD, LPDWORD, LPDWORD, LPDWORD, PFILETIME);
typedef HANDLE(WINAPI* CreateFileMappingAFn)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD,
                                             LPCSTR);
typedef HRESULT(WINAPI* CoCreateInstanceFn)(const GUID*, void*, DWORD, const GUID*, void**);
typedef HWND(WINAPI* SetFocusFn)(HWND);
typedef HWND(WINAPI* SetActiveWindowFn)(HWND);
typedef LONG(WINAPI* SetWindowLongAFn)(HWND, int, LONG);
typedef void(WINAPI* ExitProcessFn)(UINT);
typedef BOOL(WINAPI* TerminateProcessFn)(HANDLE, UINT);

struct RealApi {
    TimeGetTimeFn timeGetTime;
    QueryPerformanceCounterFn qpc;
    QueryPerformanceFrequencyFn qpf;
    GetTickCountFn getTickCount;
    GetLocalTimeFn getLocalTime;
    GetSystemTimeAsFileTimeFn systemTimeAsFileTime;
    SleepFn sleep;
    GetCursorPosFn getCursorPos;
    SetCursorPosFn setCursorPos;
    GetForegroundWindowFn getForegroundWindow;
    GetFocusFn getFocus;
    IsIconicFn isIconic;
    ShowWindowFn showWindow;
    SetForegroundWindowFn setForegroundWindow;
    CreateWindowExAFn createWindowExA;
    CreateWindowExWFn createWindowExW;
    SetWindowPosFn setWindowPos;
    BringWindowToTopFn bringWindowToTop;
    FindWindowAFn findWindowA;
    PeekMessageFn peekMessage;
    GetMessageAFn getMessage;
    DirectInput8CreateFn dinput8Create;
    CreateFileAFn createFileA;
    CreateFileWFn createFileW;
    GetFileAttributesAFn getFileAttributesA;
    DeleteFileAFn deleteFileA;
    CreateDirectoryAFn createDirectoryA;
    GetPrivateProfileStringAFn getPrivateProfileStringA;
    RegOpenKeyAFn regOpenKeyA;
    RegOpenKeyExAFn regOpenKeyExA;
    RegQueryValueExAFn regQueryValueExA;
    RegEnumKeyExAFn regEnumKeyExA;
    RegQueryInfoKeyAFn regQueryInfoKeyA;
    CreateFileMappingAFn createFileMappingA;
    CoCreateInstanceFn coCreateInstance;
    SetFocusFn setFocus;
    SetActiveWindowFn setActiveWindow;
    SetWindowLongAFn setWindowLongA;
    ExitProcessFn exitProcess;
    TerminateProcessFn terminateProcess;
} g_api;
void flush_io_locked();
void arm_io();
void read_cursor();

uint64_t g_clock_digest = 0xcbf29ce484222325ull;
unsigned g_clock_reads;

void* wrap(void* real, unsigned iface);
extern bool g_render_trace;
unsigned char* emit_thunk(unsigned char* q, unsigned iface, unsigned slot, void* real,
                          void** slot_address, bool record);
void record_args(unsigned iface, unsigned slot, const uint32_t* p);
void record_present_params(const void* params);
void add_arg_set(unsigned iface, unsigned slot, const uint32_t* v);
HRESULT __stdcall ProxyCreateDevice(void* self, unsigned adapter, unsigned device_type,
                                    HWND focus, DWORD flags, void* params, void** out);
HRESULT __stdcall ProxyPresent(void* self, const RECT* source, const RECT* dest, HWND window,
                               const void* dirty);
void capture_frame(unsigned n);

void store32(unsigned char* at, uint32_t value) { std::memcpy(at, &value, 4); }

void flush_locked(DWORD now) {
    for (unsigned i = 0; i < kTable; ++i) {
        if (!g_key[i] || g_count[i] == g_flushed[i]) continue;
        unsigned iface = (unsigned)(g_key[i] >> 56);
        unsigned slot = (unsigned)(g_key[i] >> 32) & 0xffffffu;
        std::fprintf(g_log, "call %u %u %s %08X %u\n", iface, slot, SLOT_NAMES[iface][slot],
                     (unsigned)(uint32_t)g_key[i], g_count[i] - g_flushed[i]);
        g_flushed[i] = g_count[i];
    }
    if (g_dropped) {
        std::fprintf(g_log, "# dropped %u\n", g_dropped);
        g_dropped = 0;
    }
    std::fflush(g_log);
    g_flushed_at = now;
}

extern "C" void log_call(void* caller, unsigned slot, unsigned iface) {
    if (!g_log || iface >= kIfaceCount || slot >= SLOT_COUNTS[iface]) return;
    if (g_render_trace) {
        std::fprintf(g_log, "# call %u %u %08X\n", iface, slot, (unsigned)(uintptr_t)caller);
        std::fflush(g_log);
    }
    uint64_t key = ((uint64_t)iface << 56) | ((uint64_t)slot << 32) | (uint32_t)(uintptr_t)caller;
    EnterCriticalSection(&g_lock);
    unsigned h = (unsigned)((key * 0x9e3779b97f4a7c15ull) >> 54) & (kTable - 1);
    for (unsigned probes = 0; g_key[h] && g_key[h] != key && probes < kTable; ++probes) {
        h = (h + 1) & (kTable - 1);
    }
    if (g_key[h] == key) {
        ++g_count[h];
    } else if (!g_key[h]) {
        g_key[h] = key;
        g_count[h] = 1;
        g_flushed[h] = 0;
    } else {
        ++g_dropped;
    }
    DWORD now = GetTickCount();
    if (now - g_flushed_at >= 1000) flush_locked(now);

    if ((g_io || g_replay) && iface == 1 && slot == kPresent) {
        ++g_io_frame;
        if (g_io && (g_io_frame == 1 || g_io_frame % 100 == 0)) flush_io_locked();

        read_cursor();
    }
    LeaveCriticalSection(&g_lock);
}

bool env_flag(const char* name) {
    char value[4] = {};
    return GetEnvironmentVariableA(name, value, sizeof value) > 0 && value[0] == '1';
}
bool query_log() {
    static const bool on = env_flag("TJ_QUERY_LOG");
    return on;
}
bool res_log() {
    static const bool on = env_flag("TJ_RES_LOG");
    return on;
}
void query_line_at(void* caller, const char* name, unsigned a, unsigned b, unsigned c, unsigned d,
                   HRESULT hr, const unsigned* fields, unsigned count) {
    if (!g_log || !query_log()) return;
    char line[640] = {};
    int used = std::snprintf(line, sizeof line,
                             "# d3d8 query p=%u c=%08X %s a=%08X b=%08X c=%08X d=%08X hr=%08X",
                             g_presents, (unsigned)(uintptr_t)caller, name, a, b, c, d,
                             (unsigned)hr);
    for (unsigned i = 0; i < count && used > 0 && used + 10 < (int)sizeof line; ++i) {
        used += std::snprintf(line + used, sizeof line - used, " %08X", fields[i]);
    }
    std::fprintf(g_log, "%s\n", line);
    std::fflush(g_log);
}
void create_line_at(void* caller, const char* name, unsigned a, unsigned b, unsigned c,
                    unsigned d, unsigned e, unsigned f, HRESULT hr) {
    if (!g_log || !res_log()) return;
    std::fprintf(g_log,
                 "# d3d8 create p=%u c=%08X %s a=%08X b=%08X c=%08X d=%08X e=%08X f=%08X hr=%08X\n",
                 g_presents, (unsigned)(uintptr_t)caller, name, a, b, c, d, e, f, (unsigned)hr);
    std::fflush(g_log);
}
#define query_line(...) query_line_at(_ReturnAddress(), __VA_ARGS__)
#define create_line(...) create_line_at(_ReturnAddress(), __VA_ARGS__)

unsigned g_render_calls;
char g_render_selected[16] = "legacy";
constexpr unsigned kRenderSlotAddRef = 1;
constexpr unsigned kRenderSlotRelease = 2;
constexpr unsigned kRenderSlotTextureMem = 4;
constexpr unsigned kRenderSlotBeginScene = 34;
constexpr unsigned kRenderSlotEndScene = 35;
constexpr unsigned kRenderSlotBeginStateBlock = 52;

bool g_render_trace;
void render_trace(unsigned slot) {
    if (!g_render_trace || !g_log) return;
    std::fprintf(g_log, "# render call %u %u\n", slot, (unsigned)GetTickCount());
    std::fflush(g_log);
}

bool g_draw_state;
unsigned g_draw_state_frame, g_draw_state_first, g_draw_state_last, g_draw_state_seen;
unsigned g_draw_state_present;
bool g_input_dump;
unsigned g_input_dump_frame, g_input_dump_first, g_input_dump_last;
render::Texture* g_state_texture[2];
render::Buffer* g_state_stream;
UINT g_state_stride;
render::Buffer* g_state_indices;
UINT g_state_base_vertex;

unsigned state_vertex_fvf() {
    unsigned desc[6] = {};
    if (!g_state_stream || FAILED(g_state_stream->GetDesc(desc))) return 0;
    return desc[5];
}

unsigned state_index_format() {
    unsigned desc[6] = {};
    if (!g_state_indices || FAILED(g_state_indices->GetDesc(desc))) return 101;
    return desc[0];
}

unsigned state_diffuse_offset(unsigned fvf) {
    if (!(fvf & 0x040)) return 0xFFFFFFFFu;
    unsigned offset = (fvf & 0x00Eu) == 0x004 ? 16u : 12u;
    if (fvf & 0x010) offset += 12;
    if (fvf & 0x020) offset += 4;
    return offset;
}

void state_line(const char* format, ...) {
    if (!g_log) return;
    va_list args;
    va_start(args, format);
    EnterCriticalSection(&g_lock);
    std::vfprintf(g_log, format, args);
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
    va_end(args);
}

void draw_counted() {
    unsigned frame = render::frame_number_slot();
    if (frame != g_draw_state_present) {
        g_draw_state_present = frame;
        g_draw_state_seen = 0;
    }
    ++g_draw_state_seen;
}

bool state_target() {
    return g_draw_state && render::frame_number_slot() == g_draw_state_frame &&
           g_draw_state_seen >= g_draw_state_first && g_draw_state_seen <= g_draw_state_last;
}

bool input_target() {
    return g_input_dump && render::frame_number_slot() == g_input_dump_frame &&
           g_draw_state_seen >= g_input_dump_first && g_draw_state_seen <= g_input_dump_last;
}

unsigned input_row_bytes(unsigned format, unsigned width) {
    if (format == render::kFmtDXT1) return (width + 3) / 4 * 8;
    if (format == render::kFmtDXT3 || format == render::kFmtDXT5) return (width + 3) / 4 * 16;
    return width * 4;
}

unsigned input_rows(unsigned format, unsigned height) {
    if (format == render::kFmtDXT1 || format == render::kFmtDXT3 || format == render::kFmtDXT5)
        return (height + 3) / 4;
    return height;
}

unsigned long long input_hash(const unsigned char* row, unsigned bytes,
                              unsigned long long hash) {
    for (unsigned i = 0; i < bytes; ++i) {
        hash ^= row[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

void input_texture(unsigned stage) {
    unsigned frame = render::frame_number_slot(), d = g_draw_state_seen;
    render::Texture* texture = g_state_texture[stage];
    if (!texture) {
        state_line("# input tex%u f=%u d=%u none\n", stage, frame, d);
        return;
    }
    unsigned desc[8] = {};
    if (FAILED(texture->GetLevelDesc(0, desc))) {
        state_line("# input tex%u f=%u d=%u nodesc\n", stage, frame, d);
        return;
    }
    struct Locked {
        int pitch;
        void* bits;
    } locked = {};
    unsigned format = desc[0], width = desc[6], height = desc[7];
    HRESULT hr = texture->LockRect(0, &locked, nullptr, 0x10);
    if (FAILED(hr) || !locked.bits) {
        state_line("# input tex%u f=%u d=%u fmt=%08X %ux%u lock=%08X\n", stage, frame, d, format,
                   width, height, (unsigned)hr);
        return;
    }
    unsigned rows = input_rows(format, height);
    unsigned bytes = input_row_bytes(format, width);
    unsigned long long hash = 1469598103934665603ull;
    unsigned nonzero = 0;
    for (unsigned r = 0; r < rows; ++r) {
        const unsigned char* row = (const unsigned char*)locked.bits + (size_t)r * (unsigned)locked.pitch;
        hash = input_hash(row, bytes, hash);
        for (unsigned i = 0; i < bytes; ++i) nonzero += row[i] != 0;
    }
    state_line("# input tex%u f=%u d=%u ptr=%08X fmt=%08X %ux%u pitch=%d bytes=%u hash=%016llX nz=%u\n",
               stage, frame, d, (unsigned)(uintptr_t)texture, format, width, height, locked.pitch,
               bytes * rows, hash, nonzero);
    char hex[2 * 2048 + 1] = {};
    unsigned limit = bytes * rows < 2048 ? bytes * rows : 2048;
    unsigned at = 0;
    for (unsigned r = 0; r < rows && at / 2 < limit; ++r) {
        const unsigned char* row = (const unsigned char*)locked.bits + (size_t)r * (unsigned)locked.pitch;
        for (unsigned i = 0; i < bytes && at / 2 < limit; ++i)
            at += std::snprintf(hex + at, sizeof hex - at, "%02X", row[i]);
    }
    state_line("# input tex%u f=%u d=%u data=%s\n", stage, frame, d, hex);
    texture->UnlockRect(0);
}

void input_emit(unsigned primitive, const unsigned* indices, unsigned count, unsigned start) {
    if (!input_target()) return;
    unsigned frame = render::frame_number_slot(), d = g_draw_state_seen;
    unsigned fvf = state_vertex_fvf();
    state_line("# input f=%u d=%u prim=%u count=%u start=%u stride=%u fvf=%08X base=%u\n", frame, d,
               primitive, count, start, g_state_stride, fvf, g_state_base_vertex);
    float m[16] = {};
    static const char* const kNames[4] = {"view", "proj", "world", "tmat"};
    static const unsigned kStates[4] = {2, 3, 256, 16};
    for (unsigned k = 0; k < 4; ++k) {
        if (FAILED(render::device()->GetTransform(kStates[k], m))) continue;
        state_line("# input m f=%u d=%u %s=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                   "%.9g,%.9g,%.9g,%.9g,%.9g\n",
                   frame, d, kNames[k], m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9],
                   m[10], m[11], m[12], m[13], m[14], m[15]);
    }
    char line[256] = {};
    unsigned used = 0;
    for (unsigned k = 0; k < 8 && k < count; ++k)
        used += std::snprintf(line + used, sizeof line - used, " %u", indices[k]);
    state_line("# input i f=%u d=%u%s\n", frame, d, line);
    static const unsigned kMore[14] = {47, 9, 26, 141, 161, 168, 174, 206, 154, 155, 145, 146, 147, 139};
    DWORD more[14] = {};
    for (unsigned i = 0; i < 14; ++i) render::device()->GetRenderState(kMore[i], &more[i]);
    char states[256] = {};
    used = 0;
    for (unsigned i = 0; i < 14; ++i)
        used += std::snprintf(states + used, sizeof states - used, " %u=%u", kMore[i], more[i]);
    state_line("# input rs f=%u d=%u%s\n", frame, d, states);
    unsigned position = (fvf & 0x00Eu) == 0x004 ? 16u : 12u;
    unsigned at = position;
    if (fvf & 0x010) at += 12;
    unsigned diffuse = 0xFFFFFFFFu;
    if (fvf & 0x040) {
        diffuse = at;
        at += 4;
    }
    if (fvf & 0x080) at += 4;
    unsigned coord = at;
    unsigned coords = (fvf >> 8) & 0xF;
    for (unsigned k = 0; k < 8 && k < count; ++k) {
        unsigned idx = indices[k] + g_state_base_vertex;
        unsigned char* bits = nullptr;
        if (!g_state_stream || !g_state_stride ||
            FAILED(g_state_stream->Lock(idx * g_state_stride, g_state_stride, &bits, 0x10)) ||
            !bits) {
            state_line("# input v f=%u d=%u k=%u idx=%u lock=0\n", frame, d, k, idx);
            continue;
        }
        const float* p = (const float*)bits;
        unsigned value = diffuse != 0xFFFFFFFFu && diffuse + 4 <= g_state_stride
                             ? *(const unsigned*)(bits + diffuse)
                             : 0xFFFFFFFFu;
        char uv[128] = {};
        unsigned cap = 0;
        for (unsigned s = 0; s < coords && s < 4; ++s) {
            unsigned o = coord + s * 8;
            if (o + 8 > g_state_stride) break;
            cap += std::snprintf(uv + cap, sizeof uv - cap, " %.9g,%.9g", p[o / 4], p[o / 4 + 1]);
        }
        state_line("# input v f=%u d=%u k=%u idx=%u p=%.9g,%.9g,%.9g,%.9g diff=%08X uv=%s\n", frame,
                   d, k, idx, p[0], p[1], p[2], p[3], value, uv);
        g_state_stream->Unlock();
    }
    input_texture(0);
    input_texture(1);
}

void state_emit(unsigned slot, unsigned primitive, const unsigned* indices, unsigned count,
                unsigned start) {
    DWORD rs[11] = {};
    static const unsigned kState[11] = {19, 20, 171, 27, 15, 24, 25, 7, 14, 23, 60};
    for (unsigned i = 0; i < 11; ++i) render::device()->GetRenderState(kState[i], &rs[i]);
    DWORD ts[2][13] = {};
    static const unsigned kStage[13] = {1, 2, 3, 4, 5, 6, 13, 14, 16, 17, 18, 11, 24};
    for (unsigned s = 0; s < 2; ++s) {
        for (unsigned i = 0; i < 13; ++i)
            render::device()->GetTextureStageState(s, kStage[i], &ts[s][i]);
    }
    unsigned fmt[2] = {}, tw[2] = {}, th[2] = {};
    for (unsigned s = 0; s < 2; ++s) {
        render::Texture* texture = g_state_texture[s];
        if (!texture) continue;
        unsigned desc[8] = {};
        if (SUCCEEDED(texture->GetLevelDesc(0, desc))) {
            fmt[s] = desc[0];
            tw[s] = desc[6];
            th[s] = desc[7];
        }
    }
    unsigned alpha[3] = {}, read = 0;
    unsigned fvf = state_vertex_fvf();
    unsigned offset = state_diffuse_offset(fvf);
    if (offset != 0xFFFFFFFFu && g_state_stream && g_state_stride) {
        for (unsigned k = 0; k < 3; ++k) {
            unsigned char* bits = nullptr;
            unsigned at = (indices[k] + g_state_base_vertex) * g_state_stride + offset;
            if (FAILED(g_state_stream->Lock(at, 4, &bits, 0x10)) || !bits) break;
            alpha[read++] = bits[3];
            g_state_stream->Unlock();
        }
    }
    state_line("# state f=%u d=%u slot=%u prim=%u count=%u start=%u | sblend=%u dblend=%u bop=%u "
               "blend=%u atest=%u aref=%u afunc=%u zen=%u zw=%u zf=%u tf=%08X\n",
               render::frame_number_slot(), g_draw_state_seen, slot, primitive, count, start, rs[0],
               rs[1], rs[2], rs[3], rs[4], rs[5], rs[6], rs[7], rs[8], rs[9], rs[10]);
    DWORD fog[7] = {};
    static const unsigned kFog[7] = {28, 35, 36, 37, 34, 137, 22};
    for (unsigned i = 0; i < 7; ++i) render::device()->GetRenderState(kFog[i], &fog[i]);
    state_line("# state fog f=%u d=%u en=%u tm=%u start=%u end=%u color=%08X light=%u cull=%u\n",
               render::frame_number_slot(), g_draw_state_seen, fog[0], fog[1], fog[2], fog[3],
               fog[4], fog[5], fog[6]);
    float tmat[16] = {};
    if (SUCCEEDED(render::device()->GetTransform(16, tmat)))
        state_line("# state tmat f=%u d=%u m=%f,%f,%f,%f,%f,%f,%f,%f\n",
                   render::frame_number_slot(), g_draw_state_seen, tmat[0], tmat[1], tmat[4],
                   tmat[5], tmat[8], tmat[9], tmat[12], tmat[13]);
    for (unsigned s = 0; s < 2; ++s) {
        state_line("# state tss%u col=%u/%u/%u alpha=%u/%u/%u tci=%08X ttf=%08X addr=%u/%u "
                   "fil=%u/%u/%u\n",
                   s, ts[s][0], ts[s][1], ts[s][2], ts[s][3], ts[s][4], ts[s][5], ts[s][11],
                   ts[s][12], ts[s][6], ts[s][7], ts[s][8], ts[s][9], ts[s][10]);
    }
    state_line("# state tex0=%08X fmt=%u %ux%u tex1=%08X fmt=%u %ux%u fvf=%08X stride=%u "
               "vtx_a=%u,%u,%u\n",
               g_state_texture[0] ? (unsigned)(uintptr_t)g_state_texture[0] : 0u, fmt[0], tw[0],
               th[0], g_state_texture[1] ? (unsigned)(uintptr_t)g_state_texture[1] : 0u, fmt[1],
               tw[1], th[1], fvf, g_state_stride, alpha[0], alpha[1], alpha[2]);
}

void state_draw(unsigned slot, unsigned primitive, UINT start, UINT count) {
    if (!g_draw_state && !g_input_dump) return;
    draw_counted();
    unsigned indices[8] = {};
    for (unsigned k = 0; k < 8; ++k) indices[k] = start + k;
    if (state_target()) state_emit(slot, primitive, indices, count, start);
    input_emit(primitive, indices, count, start);
}

void state_draw(unsigned slot, unsigned primitive, UINT minIndex, UINT numVertices, UINT startIndex,
                UINT count) {
    (void)minIndex;
    (void)numVertices;
    if (!g_draw_state && !g_input_dump) return;
    draw_counted();
    unsigned indices[8] = {};
    unsigned width = state_index_format() == 102 ? 4 : 2;
    unsigned char* bits = nullptr;
    unsigned wanted = count < 8 ? count : 8;
    if (g_state_indices && wanted &&
        SUCCEEDED(g_state_indices->Lock(startIndex * width, wanted * width, &bits, 0x10)) && bits) {
        for (unsigned k = 0; k < wanted; ++k)
            indices[k] = width == 4 ? ((const unsigned*)bits)[k] : ((const unsigned short*)bits)[k];
        g_state_indices->Unlock();
    }
    if (state_target()) state_emit(slot, primitive, indices, count, startIndex);
    input_emit(primitive, indices, count, startIndex);
}

void state_draw(unsigned slot, unsigned primitive, UINT count, const void* data, UINT stride) {
    (void)slot;
    (void)primitive;
    (void)count;
    (void)data;
    (void)stride;
    if (g_draw_state || g_input_dump) draw_counted();
}

template <class... A>
void state_draw(unsigned slot, const A&...) {
    (void)slot;
}

#define UNPAREN(...) __VA_ARGS__
#define STATE_ARGS(...) __VA_ARGS__
#define RENDER_ADAPT(ret, name, slot, params, args)                     \
    ret __stdcall A_##name(void* self, UNPAREN params) {                \
        (void)self;                                                     \
        log_call(_ReturnAddress(), slot, 1);                            \
        render_trace(slot);                                             \
        ++g_render_calls;                                               \
        state_draw(slot, STATE_ARGS args);                              \
        return render::device()->name args;                             \
    }
RENDER_DEVICE_CALLS(RENDER_ADAPT)
#undef RENDER_ADAPT

ULONG __stdcall A_AddRef(void* self) {
    (void)self;
    log_call(_ReturnAddress(), kRenderSlotAddRef, 1);
    ++g_render_calls;
    return render::device()->AddRef();
}
ULONG __stdcall A_Release(void* self) {
    (void)self;
    log_call(_ReturnAddress(), kRenderSlotRelease, 1);
    ++g_render_calls;
    return render::device()->Release();
}
UINT __stdcall A_GetAvailableTextureMem(void* self) {
    (void)self;
    log_call(_ReturnAddress(), kRenderSlotTextureMem, 1);
    ++g_render_calls;
    UINT available = render::device()->GetAvailableTextureMem();
    if (g_draw_state) state_line("# mem f=%u v=%u\n", g_presents, available);
    query_line("GetAvailableTextureMem", 0, 0, 0, 0, S_OK, &available, 1);
    return available;
}
HRESULT __stdcall A_BeginScene(void* self) {
    (void)self;
    log_call(_ReturnAddress(), kRenderSlotBeginScene, 1);
    ++g_render_calls;
    return render::device()->BeginScene();
}
HRESULT __stdcall A_EndScene(void* self) {
    (void)self;
    log_call(_ReturnAddress(), kRenderSlotEndScene, 1);
    ++g_render_calls;
    return render::device()->EndScene();
}
HRESULT __stdcall A_BeginStateBlock(void* self) {
    (void)self;
    log_call(_ReturnAddress(), kRenderSlotBeginStateBlock, 1);
    ++g_render_calls;
    return render::device()->BeginStateBlock();
}

struct SlotAdapter {
    unsigned slot;
    void* function;
};

constexpr unsigned kIfaceTexture = 2;
constexpr unsigned kIfaceSurface = 3;
constexpr unsigned kIfaceVertexBuffer = 4;
constexpr unsigned kIfaceIndexBuffer = 5;
constexpr unsigned kIfaceCubeTexture = 6;

void* g_device_com;

struct ResWrap {
    void** vtbl;
    void* object;
    render::Resource* backend;
    unsigned iface;
    void* container;
    unsigned level;
};

void* wrap_resource(render::Resource* backend, unsigned iface);

void addref_com(void* com) { ((ULONG(__stdcall*)(void*))(*(void***)com)[1])(com); }

template <class R, class... A>
R raw_call(render::Resource* backend, unsigned slot, A... args) {
    void* com = backend->raw();
    if (!com) return R();
    void** vtbl = *(void***)com;
    typedef R(__stdcall * Fn)(void*, A...);
    return reinterpret_cast<Fn>(vtbl[slot])(com, args...);
}

render::Texture* texture_of(void* com) {
    return com ? static_cast<render::Texture*>(static_cast<ResWrap*>(com)->object) : nullptr;
}
render::Surface* surface_of(void* com) {
    return com ? static_cast<render::Surface*>(static_cast<ResWrap*>(com)->object) : nullptr;
}
render::Buffer* buffer_of(void* com) {
    return com ? static_cast<render::Buffer*>(static_cast<ResWrap*>(com)->object) : nullptr;
}

bool g_tex_history;
unsigned g_tex_history_frame;
unsigned g_tex_bytes_frame;

bool tex_history_on() { return g_tex_history && g_presents <= g_tex_history_frame; }

struct TexLock {
    render::Texture* texture;
    unsigned level;
    const unsigned char* bits;
    unsigned pitch, bytes, rows;
};
TexLock g_tex_locks[64];

TexLock* tex_lock_record(render::Texture* texture, unsigned level, bool create) {
    TexLock* free = nullptr;
    for (TexLock& slot : g_tex_locks) {
        if (slot.texture == texture && slot.level == level) return &slot;
        if (!slot.texture && !free) free = &slot;
    }
    return create ? free : nullptr;
}

unsigned long long tex_hash(const unsigned char* bits, unsigned pitch, unsigned bytes,
                            unsigned rows) {
    unsigned long long hash = 1469598103934665603ull;
    for (unsigned r = 0; r < rows; ++r) {
        const unsigned char* row = bits + (size_t)r * pitch;
        for (unsigned i = 0; i < bytes; ++i) {
            hash ^= row[i];
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

void tex_dump(render::Texture* texture, unsigned level, const unsigned char* bits, unsigned pitch,
              unsigned bytes, unsigned rows, unsigned long long hash) {
    if (!g_tex_bytes_frame || g_presents != g_tex_bytes_frame || !bits) return;
    unsigned desc[8] = {};
    if (FAILED(texture->GetLevelDesc(level, desc)) || desc[0] != render::kFmtDXT1) return;
    static char hex[2 * 8192 + 1];
    unsigned limit = bytes * rows < 8192 ? bytes * rows : 8192;
    unsigned at = 0;
    for (unsigned r = 0; r < rows && at / 2 < limit; ++r) {
        const unsigned char* row = bits + (size_t)r * pitch;
        for (unsigned i = 0; i < bytes && at / 2 < limit; ++i)
            at += std::snprintf(hex + at, sizeof hex - at, "%02X", row[i]);
    }
    state_line("# tex hist bytes p=%u tex=%08X lev=%u hash=%016llX data=%s\n", g_presents,
               (unsigned)(uintptr_t)texture, level, hash, hex);
}

void tex_history_lock(render::Texture* texture, unsigned level, const RECT* rect, DWORD flags,
                      int pitch, const void* bits, void* caller) {
    if (!texture || !tex_history_on()) return;
    unsigned desc[8] = {};
    unsigned bytes = 0, rows = 0;
    if (SUCCEEDED(texture->GetLevelDesc(level, desc))) {
        unsigned width = rect ? (unsigned)(rect->right - rect->left) : desc[6];
        unsigned height = rect ? (unsigned)(rect->bottom - rect->top) : desc[7];
        bytes = input_row_bytes(desc[0], width);
        rows = input_rows(desc[0], height);
    }
    const unsigned char* p = (const unsigned char*)bits;
    unsigned long long pre = p && bytes && rows ? tex_hash(p, (unsigned)pitch, bytes, rows) : 0;
    TexLock* slot = tex_lock_record(texture, level, true);
    if (slot) {
        slot->texture = texture;
        slot->level = level;
        slot->bits = p;
        slot->pitch = (unsigned)pitch;
        slot->bytes = bytes;
        slot->rows = rows;
    }
    tex_dump(texture, level, p, (unsigned)pitch, bytes, rows, pre);
    unsigned short cw = 0;
    unsigned mx = 0;
    __asm fnstcw cw
    __asm stmxcsr mx
    char where[48] = "rect=none";
    if (rect)
        std::snprintf(where, sizeof where, "rect=%ld,%ld,%ld,%ld", rect->left, rect->top,
                      rect->right, rect->bottom);
    state_line("# tex hist lock p=%u c=%08X tex=%08X lev=%u fmt=%08X %s flags=%08X pitch=%d "
               "bits=%08X pre=%016llX cw=%04X mx=%08X\n",
               g_presents, (unsigned)(uintptr_t)caller, (unsigned)(uintptr_t)texture, level,
               desc[0], where, (unsigned)flags, pitch, (unsigned)(uintptr_t)bits, pre, cw, mx);
}

void tex_history_unlock(render::Texture* texture, unsigned level, void* caller) {
    if (!texture) return;
    TexLock* slot = tex_lock_record(texture, level, false);
    if (!slot) return;
    unsigned long long post = slot->bits && slot->bytes && slot->rows
                                  ? tex_hash(slot->bits, slot->pitch, slot->bytes, slot->rows)
                                  : 0;
    if (tex_history_on()) {
        unsigned short cw = 0;
        unsigned mx = 0;
        __asm fnstcw cw
        __asm stmxcsr mx
        state_line("# tex hist unlock p=%u c=%08X tex=%08X lev=%u post=%016llX cw=%04X mx=%08X\n",
                   g_presents, (unsigned)(uintptr_t)caller, (unsigned)(uintptr_t)texture, level,
                   post, cw, mx);
    }
    tex_dump(texture, level, slot->bits, slot->pitch, slot->bytes, slot->rows, post);
    slot->texture = nullptr;
}

void tex_history_copy(void* source, const RECT* rects, UINT count, void* dest, const POINT* points,
                      HRESULT hr, void* caller) {
    if (!tex_history_on()) return;
    ResWrap* s = source ? static_cast<ResWrap*>(source) : nullptr;
    ResWrap* d = dest ? static_cast<ResWrap*>(dest) : nullptr;
    state_line("# tex hist copy p=%u c=%08X n=%u hr=%08X src=%08X srctex=%08X srclev=%u dst=%08X "
               "dsttex=%08X dstlev=%u rect=%ld,%ld,%ld,%ld pt=%ld,%ld\n",
               g_presents, (unsigned)(uintptr_t)caller, count, (unsigned)hr,
               (unsigned)(uintptr_t)(s ? s->object : nullptr),
               (unsigned)(uintptr_t)(s && s->container ? texture_of(s->container) : nullptr),
               s ? s->level : 0, (unsigned)(uintptr_t)(d ? d->object : nullptr),
               (unsigned)(uintptr_t)(d && d->container ? texture_of(d->container) : nullptr),
               d ? d->level : 0, rects ? rects[0].left : 0, rects ? rects[0].top : 0,
               rects ? rects[0].right : 0, rects ? rects[0].bottom : 0,
               points ? points[0].x : 0, points ? points[0].y : 0);
}

void tex_history_update(void* source, void* dest, HRESULT hr, void* caller) {
    if (!tex_history_on()) return;
    state_line("# tex hist update p=%u c=%08X hr=%08X src=%08X dst=%08X\n", g_presents,
               (unsigned)(uintptr_t)caller, (unsigned)hr, (unsigned)(uintptr_t)texture_of(source),
               (unsigned)(uintptr_t)texture_of(dest));
}

void tex_history_create(render::Texture* texture, UINT width, UINT height, UINT levels,
                        DWORD usage, DWORD format, DWORD pool, void* caller) {
    if (!tex_history_on()) return;
    state_line("# tex hist create p=%u c=%08X tex=%08X %ux%u lev=%u fmt=%08X usage=%u pool=%u\n",
               g_presents, (unsigned)(uintptr_t)caller, (unsigned)(uintptr_t)texture, width, height,
               levels, (unsigned)format, (unsigned)usage, (unsigned)pool);
}

#define RES_CALL(slot)                                                  \
    ResWrap* w = static_cast<ResWrap*>(self);                           \
    log_call(_ReturnAddress(), slot, w->iface);                         \
    ++g_render_calls;                                                   \
    if (tex_history_on())                                               \
        state_line("# tex hist call p=%u sl=%u c=%08X if=%u\n", g_presents, slot, \
                   (unsigned)(uintptr_t)_ReturnAddress(), w->iface);

HRESULT __stdcall R_QueryInterface(void* self, const void* riid, void** out) {
    (void)riid;
    RES_CALL(0)
    if (out) {
        addref_com(self);
        *out = self;
    }
    return S_OK;
}
ULONG __stdcall R_AddRef(void* self) {
    RES_CALL(1)
    return w->backend->AddRef();
}
ULONG __stdcall R_Release(void* self) {
    RES_CALL(2)
    ULONG left = w->backend->Release();
    if (!left) {
        delete w->backend;
        delete w;
    }
    return left;
}

HRESULT __stdcall R_GetDevice(void* self, void** out) {
    RES_CALL(3)
    void* provider = nullptr;
    HRESULT hr = w->backend->GetDevice(&provider);
    if (FAILED(hr) || !out) return hr;
    if (!provider || provider == g_device_com) {
        *out = provider;
        return hr;
    }
    ((ULONG(__stdcall*)(void*))(*(void***)provider)[2])(provider);
    addref_com(g_device_com);
    *out = g_device_com;
    return hr;
}
HRESULT __stdcall R_SetPrivateData(void* self, const void* guid, const void* data, DWORD size,
                                   DWORD flags) {
    RES_CALL(4)
    return raw_call<HRESULT>(w->backend, 4, guid, data, size, flags);
}
HRESULT __stdcall R_GetPrivateData(void* self, const void* guid, void* data, DWORD* size) {
    RES_CALL(5)
    return raw_call<HRESULT>(w->backend, 5, guid, data, size);
}
HRESULT __stdcall R_FreePrivateData(void* self, const void* guid) {
    RES_CALL(6)
    return raw_call<HRESULT>(w->backend, 6, guid);
}
DWORD __stdcall R_SetPriority(void* self, DWORD value) {
    RES_CALL(7)
    return raw_call<DWORD>(w->backend, 7, value);
}
DWORD __stdcall R_GetPriority(void* self) {
    RES_CALL(8)
    return raw_call<DWORD>(w->backend, 8);
}
void __stdcall R_PreLoad(void* self) {
    RES_CALL(9)
    if (w->iface == kIfaceTexture || w->iface == kIfaceCubeTexture) {
        static_cast<render::Texture*>(w->object)->PreLoad();
    } else {
        raw_call<void>(w->backend, 9);
    }
}
DWORD resource_type(unsigned iface) {
    switch (iface) {
        case kIfaceSurface: return 1;
        case kIfaceTexture: return 3;
        case kIfaceCubeTexture: return 5;
        case kIfaceVertexBuffer: return 6;
        case kIfaceIndexBuffer: return 7;
        default: return 0;
    }
}
DWORD __stdcall R_GetType(void* self) {
    RES_CALL(10)

    if (!w->backend->raw()) return resource_type(w->iface);
    return raw_call<DWORD>(w->backend, 10);
}
DWORD __stdcall R_SetLOD(void* self, DWORD lod) {
    RES_CALL(11)
    return raw_call<DWORD>(w->backend, 11, lod);
}
DWORD __stdcall R_GetLOD(void* self) {
    RES_CALL(12)
    return raw_call<DWORD>(w->backend, 12);
}
DWORD __stdcall R_GetLevelCount(void* self) {
    RES_CALL(13)
    return static_cast<render::Texture*>(w->object)->GetLevelCount();
}
HRESULT __stdcall R_TextureGetLevelDesc(void* self, UINT level, void* desc) {
    RES_CALL(14)
    return static_cast<render::Texture*>(w->object)->GetLevelDesc(level, desc);
}
HRESULT __stdcall R_TextureGetSurfaceLevel(void* self, UINT level, void** out) {
    RES_CALL(15)
    render::Surface* surface = nullptr;
    HRESULT hr = static_cast<render::Texture*>(w->object)->GetSurface(0, level, &surface);
    if (SUCCEEDED(hr) && surface) {
        *out = wrap_resource(surface, kIfaceSurface);
        static_cast<ResWrap*>(*out)->container = self;
        static_cast<ResWrap*>(*out)->level = level;
    }
    return hr;
}
HRESULT __stdcall R_CubeGetSurface(void* self, unsigned face, UINT level, void** out) {
    RES_CALL(15)
    render::Surface* surface = nullptr;
    HRESULT hr = static_cast<render::Texture*>(w->object)->GetSurface(face, level, &surface);
    if (SUCCEEDED(hr) && surface) {
        *out = wrap_resource(surface, kIfaceSurface);
        static_cast<ResWrap*>(*out)->container = self;
        static_cast<ResWrap*>(*out)->level = level;
    }
    return hr;
}
HRESULT __stdcall R_TextureLockRect(void* self, UINT level, void* locked, const RECT* rect,
                                    DWORD flags) {
    RES_CALL(16)
    void* caller = _ReturnAddress();
    HRESULT hr = static_cast<render::Texture*>(w->object)->LockRect(level, locked, rect, flags);
    if (SUCCEEDED(hr) && locked)
        tex_history_lock(static_cast<render::Texture*>(w->object), level, rect, flags,
                         ((const int*)locked)[0], *(void* const*)((const char*)locked + 4), caller);
    return hr;
}
HRESULT __stdcall R_TextureUnlockRect(void* self, UINT level) {
    RES_CALL(17)
    tex_history_unlock(static_cast<render::Texture*>(w->object), level, _ReturnAddress());
    return static_cast<render::Texture*>(w->object)->UnlockRect(level);
}
HRESULT __stdcall R_AddDirtyRect(void* self, const RECT* rect) {
    RES_CALL(18)
    return raw_call<HRESULT>(w->backend, 18, rect);
}
HRESULT __stdcall R_SurfaceGetContainer(void* self, const void* riid, void** out) {
    RES_CALL(7)
    if (w->container && out) {
        addref_com(w->container);
        *out = w->container;
        return S_OK;
    }
    return raw_call<HRESULT>(w->backend, 7, riid, out);
}
HRESULT __stdcall R_SurfaceGetDesc(void* self, void* desc) {
    RES_CALL(8)
    HRESULT hr = static_cast<render::Surface*>(w->object)->GetDesc(desc);
    if (SUCCEEDED(hr) && desc && tex_history_on()) {
        const unsigned* d = static_cast<const unsigned*>(desc);
        state_line("# tex hist desc p=%u surf=%08X d=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X\n",
                   g_presents, (unsigned)(uintptr_t)w->object, d[0], d[1], d[2], d[3], d[4], d[5],
                   d[6], d[7]);
    }
    return hr;
}
HRESULT __stdcall R_SurfaceLockRect(void* self, void* locked, const RECT* rect, DWORD flags) {
    RES_CALL(9)
    void* caller = _ReturnAddress();
    HRESULT hr = static_cast<render::Surface*>(w->object)->LockRect(locked, rect, flags);
    if (SUCCEEDED(hr) && locked && w->container)
        tex_history_lock(texture_of(w->container), w->level, rect, flags,
                         ((const int*)locked)[0], *(void* const*)((const char*)locked + 4), caller);
    return hr;
}
HRESULT __stdcall R_SurfaceUnlockRect(void* self) {
    RES_CALL(10)
    if (w->container) tex_history_unlock(texture_of(w->container), w->level, _ReturnAddress());
    return static_cast<render::Surface*>(w->object)->UnlockRect();
}
HRESULT __stdcall R_BufferLock(void* self, UINT offset, UINT size, unsigned char** bits,
                               DWORD flags) {
    RES_CALL(11)
    return static_cast<render::Buffer*>(w->object)->Lock(offset, size, bits, flags);
}
HRESULT __stdcall R_BufferUnlock(void* self) {
    RES_CALL(12)
    return static_cast<render::Buffer*>(w->object)->Unlock();
}
HRESULT __stdcall R_BufferGetDesc(void* self, void* desc) {
    RES_CALL(13)
    return static_cast<render::Buffer*>(w->object)->GetDesc(desc);
}
#undef RES_CALL

void* const kTextureVtbl[] = {
    (void*)&R_QueryInterface, (void*)&R_AddRef,        (void*)&R_Release,
    (void*)&R_GetDevice,      (void*)&R_SetPrivateData, (void*)&R_GetPrivateData,
    (void*)&R_FreePrivateData, (void*)&R_SetPriority,  (void*)&R_GetPriority,
    (void*)&R_PreLoad,        (void*)&R_GetType,       (void*)&R_SetLOD,
    (void*)&R_GetLOD,         (void*)&R_GetLevelCount, (void*)&R_TextureGetLevelDesc,
    (void*)&R_TextureGetSurfaceLevel, (void*)&R_TextureLockRect, (void*)&R_TextureUnlockRect,
    (void*)&R_AddDirtyRect,
};
void* const kCubeVtbl[] = {
    (void*)&R_QueryInterface, (void*)&R_AddRef,        (void*)&R_Release,
    (void*)&R_GetDevice,      (void*)&R_SetPrivateData, (void*)&R_GetPrivateData,
    (void*)&R_FreePrivateData, (void*)&R_SetPriority,  (void*)&R_GetPriority,
    (void*)&R_PreLoad,        (void*)&R_GetType,       (void*)&R_SetLOD,
    (void*)&R_GetLOD,         (void*)&R_GetLevelCount, (void*)&R_TextureGetLevelDesc,
    (void*)&R_CubeGetSurface, (void*)&R_TextureLockRect, (void*)&R_TextureUnlockRect,
    (void*)&R_AddDirtyRect,
};
void* const kSurfaceVtbl[] = {
    (void*)&R_QueryInterface, (void*)&R_AddRef,         (void*)&R_Release,
    (void*)&R_GetDevice,      (void*)&R_SetPrivateData, (void*)&R_GetPrivateData,
    (void*)&R_FreePrivateData, (void*)&R_SurfaceGetContainer, (void*)&R_SurfaceGetDesc,
    (void*)&R_SurfaceLockRect, (void*)&R_SurfaceUnlockRect,
};
void* const kBufferVtbl[] = {
    (void*)&R_QueryInterface, (void*)&R_AddRef,        (void*)&R_Release,
    (void*)&R_GetDevice,      (void*)&R_SetPrivateData, (void*)&R_GetPrivateData,
    (void*)&R_FreePrivateData, (void*)&R_SetPriority,  (void*)&R_GetPriority,
    (void*)&R_PreLoad,        (void*)&R_GetType,       (void*)&R_BufferLock,
    (void*)&R_BufferUnlock,   (void*)&R_BufferGetDesc,
};
void* wrap_resource(render::Resource* backend, unsigned iface) {
    ResWrap* w = new ResWrap;
    w->object = backend;
    w->backend = backend;
    w->iface = iface;
    w->container = nullptr;
    w->level = 0;
    if (iface == kIfaceTexture) w->vtbl = (void**)kTextureVtbl;
    else if (iface == kIfaceCubeTexture) w->vtbl = (void**)kCubeVtbl;
    else if (iface == kIfaceSurface) w->vtbl = (void**)kSurfaceVtbl;
    else w->vtbl = (void**)kBufferVtbl;
    return w;
}

HRESULT note(unsigned slot, HRESULT hr) {
    if (g_render_trace && g_log) {
        std::fprintf(g_log, "# render hr %u %08X\n", slot, (unsigned)hr);
        std::fflush(g_log);
    }
    return hr;
}

#define RENDER_CALL(slot)                 \
    log_call(_ReturnAddress(), slot, 1);  \
    render_trace(slot);                   \
    ++g_render_calls;

HRESULT __stdcall A_GetBackBuffer(void* self, UINT index, unsigned type, void** out) {
    (void)self;
    RENDER_CALL(16)
    render::Surface* surface = nullptr;
    HRESULT hr = render::device()->GetBackBuffer(index, type, &surface);
    if (out) *out = SUCCEEDED(hr) && surface ? wrap_resource(surface, kIfaceSurface) : nullptr;
    return note(16, hr);
}
HRESULT __stdcall A_CreateTexture(void* self, UINT width, UINT height, UINT levels, DWORD usage,
                                  DWORD format, DWORD pool, void** out) {
    (void)self;
    RENDER_CALL(20)
    render::Texture* texture = nullptr;
    HRESULT hr = render::device()->CreateTexture(width, height, levels, usage, format, pool,
                                                 &texture);
    if (g_draw_state)
        state_line("# create tex=%08X %ux%u lev=%u fmt=%08X usage=%u pool=%u\n",
                   (unsigned)(uintptr_t)texture, width, height, levels, (unsigned)format,
                   (unsigned)usage, (unsigned)pool);
    create_line("CreateTexture", width, height, levels, format, usage, pool, hr);
    if (SUCCEEDED(hr) && texture)
        tex_history_create(texture, width, height, levels, usage, format, pool, _ReturnAddress());
    if (out) *out = SUCCEEDED(hr) && texture ? wrap_resource(texture, kIfaceTexture) : nullptr;
    return note(20, hr);
}
HRESULT __stdcall A_CreateCubeTexture(void* self, UINT edge, UINT levels, DWORD usage,
                                      DWORD format, DWORD pool, void** out) {
    (void)self;
    RENDER_CALL(22)
    render::Texture* texture = nullptr;
    HRESULT hr = render::device()->CreateCubeTexture(edge, levels, usage, format, pool, &texture);
    create_line("CreateCubeTexture", edge, levels, format, usage, pool, 0, hr);
    if (out) *out = SUCCEEDED(hr) && texture ? wrap_resource(texture, kIfaceCubeTexture) : nullptr;
    return note(22, hr);
}
HRESULT __stdcall A_CreateVertexBuffer(void* self, UINT length, DWORD usage, DWORD fvf,
                                       DWORD pool, void** out) {
    (void)self;
    RENDER_CALL(23)
    render::Buffer* buffer = nullptr;
    HRESULT hr = render::device()->CreateVertexBuffer(length, usage, fvf, pool, &buffer);
    create_line("CreateVertexBuffer", length, usage, fvf, pool, 0, 0, hr);
    if (out) *out = SUCCEEDED(hr) && buffer ? wrap_resource(buffer, kIfaceVertexBuffer) : nullptr;
    return note(23, hr);
}
HRESULT __stdcall A_CreateIndexBuffer(void* self, UINT length, DWORD usage, DWORD format,
                                      DWORD pool, void** out) {
    (void)self;
    RENDER_CALL(24)
    render::Buffer* buffer = nullptr;
    HRESULT hr = render::device()->CreateIndexBuffer(length, usage, format, pool, &buffer);
    create_line("CreateIndexBuffer", length, usage, format, pool, 0, 0, hr);
    if (out) *out = SUCCEEDED(hr) && buffer ? wrap_resource(buffer, kIfaceIndexBuffer) : nullptr;
    return note(24, hr);
}
HRESULT __stdcall A_CreateDepthStencilSurface(void* self, UINT width, UINT height, DWORD format,
                                              unsigned multisample, void** out) {
    (void)self;
    RENDER_CALL(26)
    render::Surface* surface = nullptr;
    HRESULT hr = render::device()->CreateDepthStencilSurface(width, height, format, multisample,
                                                             &surface);
    create_line("CreateDepthStencilSurface", width, height, format, multisample, 0, 0, hr);
    if (out) *out = SUCCEEDED(hr) && surface ? wrap_resource(surface, kIfaceSurface) : nullptr;
    return note(26, hr);
}
HRESULT __stdcall A_CreateImageSurface(void* self, UINT width, UINT height, DWORD format,
                                       void** out) {
    (void)self;
    RENDER_CALL(27)
    render::Surface* surface = nullptr;
    HRESULT hr = render::device()->CreateImageSurface(width, height, format, &surface);
    create_line("CreateImageSurface", width, height, format, 0, 0, 0, hr);
    if (out) *out = SUCCEEDED(hr) && surface ? wrap_resource(surface, kIfaceSurface) : nullptr;
    return note(27, hr);
}
HRESULT __stdcall A_CopyRects(void* self, void* source, const RECT* rects, UINT count, void* dest,
                              const POINT* points) {
    (void)self;
    RENDER_CALL(28)
    HRESULT hr = render::device()->CopyRects(surface_of(source), rects, count, surface_of(dest),
                                             points);
    tex_history_copy(source, rects, count, dest, points, hr, _ReturnAddress());
    return note(28, hr);
}
HRESULT __stdcall A_UpdateTexture(void* self, void* source, void* dest) {
    (void)self;
    RENDER_CALL(29)
    HRESULT hr = render::device()->UpdateTexture(texture_of(source), texture_of(dest));
    tex_history_update(source, dest, hr, _ReturnAddress());
    return note(29, hr);
}
HRESULT __stdcall A_SetRenderTarget(void* self, void* target, void* depth) {
    (void)self;
    RENDER_CALL(31)
    return note(31, render::device()->SetRenderTarget(surface_of(target), surface_of(depth)));
}
HRESULT __stdcall A_GetRenderTarget(void* self, void** out) {
    (void)self;
    RENDER_CALL(32)
    render::Surface* surface = nullptr;
    HRESULT hr = render::device()->GetRenderTarget(&surface);
    if (out) *out = SUCCEEDED(hr) && surface ? wrap_resource(surface, kIfaceSurface) : nullptr;
    return note(32, hr);
}
HRESULT __stdcall A_GetDepthStencilSurface(void* self, void** out) {
    (void)self;
    RENDER_CALL(33)
    render::Surface* surface = nullptr;
    HRESULT hr = render::device()->GetDepthStencilSurface(&surface);
    if (out) *out = SUCCEEDED(hr) && surface ? wrap_resource(surface, kIfaceSurface) : nullptr;
    return note(33, hr);
}
HRESULT __stdcall A_SetTexture(void* self, DWORD stage, void* texture) {
    (void)self;
    RENDER_CALL(61)
    if ((g_draw_state || g_input_dump) && stage < 2) g_state_texture[stage] = texture_of(texture);
    return note(61, render::device()->SetTexture(stage, texture_of(texture)));
}
enum { kStageOffNone = 0, kStageOffTexture, kStageOffFog, kStageOffLight, kStageOffBlend };

unsigned stage_off() {
    static unsigned value = 0xFFFFFFFFu;
    if (value == 0xFFFFFFFFu) {
        char text[16] = {};
        GetEnvironmentVariableA("TJ_STAGE_OFF", text, sizeof text);
        value = kStageOffNone;
        if (!std::strcmp(text, "texture")) value = kStageOffTexture;
        else if (!std::strcmp(text, "fog")) value = kStageOffFog;
        else if (!std::strcmp(text, "light")) value = kStageOffLight;
        else if (!std::strcmp(text, "blend")) value = kStageOffBlend;
    }
    return value;
}

HRESULT __stdcall A_SetStateOff(void* self, unsigned state, DWORD value) {
    (void)self;
    log_call(_ReturnAddress(), kSetRenderState, 1);
    render_trace(kSetRenderState);
    ++g_render_calls;
    unsigned off = stage_off();
    if ((off == kStageOffFog && state == 28) || (off == kStageOffLight && state == 137) ||
        (off == kStageOffBlend && state == 27)) value = 0;
    return render::device()->SetRenderState(state, value);
}

HRESULT __stdcall A_SetTextureOff(void* self, DWORD stage, void* texture) {
    (void)self;
    log_call(_ReturnAddress(), 61, 1);
    render_trace(61);
    ++g_render_calls;
    if ((g_draw_state || g_input_dump) && stage < 2) g_state_texture[stage] = texture_of(texture);
    if (stage_off() == kStageOffTexture) texture = nullptr;
    return note(61, render::device()->SetTexture(stage, texture_of(texture)));
}
HRESULT __stdcall A_SetStreamSource(void* self, UINT stream, void* buffer, UINT stride) {
    (void)self;
    RENDER_CALL(83)
    if (g_draw_state || g_input_dump) {
        g_state_stream = buffer_of(buffer);
        g_state_stride = stride;
    }
    return note(83, render::device()->SetStreamSource(stream, buffer_of(buffer), stride));
}
HRESULT __stdcall A_SetIndices(void* self, void* buffer, UINT baseVertex) {
    (void)self;
    RENDER_CALL(85)
    if (g_draw_state || g_input_dump) {
        g_state_indices = buffer_of(buffer);
        g_state_base_vertex = baseVertex;
    }
    return note(85, render::device()->SetIndices(buffer_of(buffer), baseVertex));
}
#undef RENDER_CALL

HRESULT __stdcall A_QueryDeviceCaps(void* self, void* caps) {
    (void)self;
    log_call(_ReturnAddress(), 7, 1);
    ++g_render_calls;
    HRESULT hr = render::device()->GetDeviceCaps(caps);
    query_line("DeviceGetDeviceCaps", 0, 0, 0, 0, hr, (const unsigned*)caps,
               SUCCEEDED(hr) && caps ? 52 : 0);
    return hr;
}
HRESULT __stdcall A_QueryDisplayMode(void* self, void* mode) {
    (void)self;
    log_call(_ReturnAddress(), 8, 1);
    ++g_render_calls;
    HRESULT hr = render::device()->GetDisplayMode(mode);
    query_line("GetDisplayMode", 0, 0, 0, 0, hr, (const unsigned*)mode,
               SUCCEEDED(hr) && mode ? 4 : 0);
    return hr;
}
HRESULT __stdcall A_QueryValidateDevice(void* self, DWORD* passes) {
    (void)self;
    log_call(_ReturnAddress(), 64, 1);
    ++g_render_calls;
    HRESULT hr = render::device()->ValidateDevice(passes);
    query_line("ValidateDevice", 0, 0, 0, 0, hr, (const unsigned*)passes, passes ? 1 : 0);
    return hr;
}
HRESULT __stdcall A_QueryInfo(void* self, DWORD id, void* info, DWORD size) {
    (void)self;
    log_call(_ReturnAddress(), 65, 1);
    ++g_render_calls;
    HRESULT hr = render::device()->GetInfo(id, info, size);
    query_line("GetInfo", id, size, 0, 0, hr, (const unsigned*)info, 0);
    return hr;
}

HRESULT __stdcall A_Root(void* self, void** out) {
    (void)self;
    log_call(_ReturnAddress(), 6, 1);
    ++g_render_calls;
    void* root = render::root_com();
    if (!root) return render::device()->GetDirect3D(out);
    if (!out) return E_POINTER;
    ((ULONG(__stdcall*)(void*))(*(void***)root)[1])(root);
    *out = root;
    return S_OK;
}

const SlotAdapter kAdapters[] = {
#define RENDER_ROW(ret, name, slot, params, args) {slot, (void*)&A_##name},
    RENDER_DEVICE_CALLS(RENDER_ROW)
#undef RENDER_ROW
    {6, (void*)&A_Root},
    {kRenderSlotAddRef, (void*)&A_AddRef},
    {kRenderSlotRelease, (void*)&A_Release},
    {kRenderSlotTextureMem, (void*)&A_GetAvailableTextureMem},
    {kRenderSlotBeginScene, (void*)&A_BeginScene},
    {kRenderSlotEndScene, (void*)&A_EndScene},
    {kRenderSlotBeginStateBlock, (void*)&A_BeginStateBlock},
    {16, (void*)&A_GetBackBuffer},
    {20, (void*)&A_CreateTexture},
    {22, (void*)&A_CreateCubeTexture},
    {23, (void*)&A_CreateVertexBuffer},
    {24, (void*)&A_CreateIndexBuffer},
    {26, (void*)&A_CreateDepthStencilSurface},
    {27, (void*)&A_CreateImageSurface},
    {28, (void*)&A_CopyRects},
    {29, (void*)&A_UpdateTexture},
    {31, (void*)&A_SetRenderTarget},
    {32, (void*)&A_GetRenderTarget},
    {33, (void*)&A_GetDepthStencilSurface},
    {61, (void*)&A_SetTexture},
    {83, (void*)&A_SetStreamSource},
    {85, (void*)&A_SetIndices},
    {7, (void*)&A_QueryDeviceCaps},
    {8, (void*)&A_QueryDisplayMode},
    {64, (void*)&A_QueryValidateDevice},
    {65, (void*)&A_QueryInfo},
    {kSetRenderState, (void*)&A_SetStateOff},
    {61, (void*)&A_SetTextureOff},
};

struct RootAdapter {
    unsigned slot;
    void* function;
};

UINT __stdcall P_GetAdapterCount(void* self) {
    log_call(_ReturnAddress(), 4, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef UINT(__stdcall * Fn)(void*);
    UINT count = ((Fn)(*(void***)w->real)[4])(w->real);
    query_line("GetAdapterCount", 0, 0, 0, 0, S_OK, &count, 1);
    return count;
}
HRESULT __stdcall P_GetAdapterIdentifier(void* self, UINT adapter, DWORD flags, void* out) {
    log_call(_ReturnAddress(), 5, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef HRESULT(__stdcall * Fn)(void*, UINT, DWORD, void*);
    HRESULT hr = ((Fn)(*(void***)w->real)[5])(w->real, adapter, flags, out);
    unsigned values[5] = {};
    if (SUCCEEDED(hr) && out) {
        const unsigned* p = (const unsigned*)((const unsigned char*)out + 1032);
        for (unsigned i = 0; i < 4; ++i) values[i] = p[i];
        values[4] = ((const unsigned*)out)[1064 / 4];
    }
    query_line("GetAdapterIdentifier", adapter, flags, 0, 0, hr, values,
               SUCCEEDED(hr) && out ? 5 : 0);
    return hr;
}
UINT __stdcall P_GetAdapterModeCount(void* self, UINT adapter) {
    log_call(_ReturnAddress(), 6, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef UINT(__stdcall * Fn)(void*, UINT);
    UINT count = ((Fn)(*(void***)w->real)[6])(w->real, adapter);
    query_line("GetAdapterModeCount", adapter, 0, 0, 0, S_OK, &count, 1);
    return count;
}
HRESULT __stdcall P_EnumAdapterModes(void* self, UINT adapter, UINT mode, void* out) {
    log_call(_ReturnAddress(), 7, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef HRESULT(__stdcall * Fn)(void*, UINT, UINT, void*);
    HRESULT hr = ((Fn)(*(void***)w->real)[7])(w->real, adapter, mode, out);
    query_line("EnumAdapterModes", adapter, mode, 0, 0, hr, (const unsigned*)out,
               SUCCEEDED(hr) && out ? 4 : 0);
    return hr;
}
HRESULT __stdcall P_GetAdapterDisplayMode(void* self, UINT adapter, void* out) {
    log_call(_ReturnAddress(), 8, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef HRESULT(__stdcall * Fn)(void*, UINT, void*);
    HRESULT hr = ((Fn)(*(void***)w->real)[8])(w->real, adapter, out);
    query_line("GetAdapterDisplayMode", adapter, 0, 0, 0, hr, (const unsigned*)out,
               SUCCEEDED(hr) && out ? 4 : 0);
    return hr;
}
HRESULT __stdcall P_CheckDeviceType(void* self, UINT adapter, unsigned type, DWORD display,
                                    DWORD back, BOOL windowed) {
    log_call(_ReturnAddress(), 9, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef HRESULT(__stdcall * Fn)(void*, UINT, unsigned, DWORD, DWORD, BOOL);
    HRESULT hr = ((Fn)(*(void***)w->real)[9])(w->real, adapter, type, display, back, windowed);
    unsigned extra[2] = {adapter, type};
    query_line("CheckDeviceType", display, back, windowed, type, hr, extra, 2);
    return hr;
}
HRESULT __stdcall P_CheckDeviceFormat(void* self, UINT adapter, unsigned type, DWORD adapterFormat,
                                      DWORD usage, unsigned rtype, DWORD checkFormat) {
    log_call(_ReturnAddress(), 10, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef HRESULT(__stdcall * Fn)(void*, UINT, unsigned, DWORD, DWORD, unsigned, DWORD);
    HRESULT hr = ((Fn)(*(void***)w->real)[10])(w->real, adapter, type, adapterFormat, usage, rtype,
                                               checkFormat);
    unsigned extra[2] = {adapter, type};
    query_line("CheckDeviceFormat", adapterFormat, usage, rtype, checkFormat, hr, extra, 2);
    return hr;
}
HRESULT __stdcall P_CheckDeviceMultiSampleType(void* self, UINT adapter, unsigned type,
                                               DWORD surfaceFormat, BOOL windowed,
                                               unsigned multisample) {
    log_call(_ReturnAddress(), 11, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef HRESULT(__stdcall * Fn)(void*, UINT, unsigned, DWORD, BOOL, unsigned);
    HRESULT hr = ((Fn)(*(void***)w->real)[11])(w->real, adapter, type, surfaceFormat, windowed,
                                               multisample);
    unsigned extra[2] = {adapter, type};
    query_line("CheckDeviceMultiSampleType", surfaceFormat, windowed, multisample, 0, hr, extra, 2);
    return hr;
}
HRESULT __stdcall P_CheckDepthStencilMatch(void* self, UINT adapter, unsigned type,
                                           DWORD adapterFormat, DWORD renderTargetFormat,
                                           DWORD depthFormat) {
    log_call(_ReturnAddress(), 12, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef HRESULT(__stdcall * Fn)(void*, UINT, unsigned, DWORD, DWORD, DWORD);
    HRESULT hr = ((Fn)(*(void***)w->real)[12])(w->real, adapter, type, adapterFormat,
                                               renderTargetFormat, depthFormat);
    unsigned extra[2] = {adapter, type};
    query_line("CheckDepthStencilMatch", adapterFormat, renderTargetFormat, depthFormat, 0, hr,
               extra, 2);
    return hr;
}
HRESULT __stdcall P_GetDeviceCaps(void* self, UINT adapter, unsigned type, void* caps) {
    log_call(_ReturnAddress(), 13, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    typedef HRESULT(__stdcall * Fn)(void*, UINT, unsigned, void*);
    HRESULT hr = ((Fn)(*(void***)w->real)[13])(w->real, adapter, type, caps);
    query_line("GetDeviceCaps", adapter, type, 0, 0, hr, (const unsigned*)caps,
               SUCCEEDED(hr) && caps ? 52 : 0);
    return hr;
}

const RootAdapter kRootAdapters[] = {
    {4, (void*)&P_GetAdapterCount},
    {5, (void*)&P_GetAdapterIdentifier},
    {6, (void*)&P_GetAdapterModeCount},
    {7, (void*)&P_EnumAdapterModes},
    {8, (void*)&P_GetAdapterDisplayMode},
    {9, (void*)&P_CheckDeviceType},
    {10, (void*)&P_CheckDeviceFormat},
    {11, (void*)&P_CheckDeviceMultiSampleType},
    {12, (void*)&P_CheckDepthStencilMatch},
    {13, (void*)&P_GetDeviceCaps},
};

const char* io_role(unsigned role) {
    static const char* const kRoles[] = {"unk", "keyboard", "joystick", "mouse"};
    return kRoles[role < 4 ? role : 0];
}

const char* io_name(unsigned id) {
    static const char* const kApis[] = {"", "timeGetTime", "QueryPerformanceCounter",
                                        "QueryPerformanceFrequency", "GetTickCount",
                                        "GetLocalTime", "GetSystemTimeAsFileTime", "Sleep",
                                        "GetCursorPos", "SetCursorPos", "GetForegroundWindow",
                                        "GetFocus", "IsIconic", "ShowWindow",
                                        "SetForegroundWindow", "CreateWindowExA", "SetWindowPos", "BringWindowToTop", "PeekMessageA",
                                        "CreateFileA", "CreateFileW",
                                        "GetFileAttributesA", "DeleteFileA", "CreateDirectoryA",
                                        "GetPrivateProfileStringA", "RegOpenKeyA",
                                        "RegOpenKeyExA", "RegQueryValueExA", "RegEnumKeyExA",
                                        "RegQueryInfoKeyA", "CreateFileMappingA",
                                        "CoCreateInstance", "SetFocus", "SetWindowLongA",
                                        "SetActiveWindow", "CreateWindowExA", "CreateWindowExW",
                                        "FindWindowA"};
    static const char* const kMethods[] = {
        "QueryInterface", "AddRef", "Release", "GetCapabilities", "EnumObjects",
        "GetProperty", "SetProperty", "Acquire", "Unacquire", "GetDeviceState",
        "GetDeviceData", "SetDataFormat", "SetEventNotification", "SetCooperativeLevel",
        "GetObjectInfo", "GetDeviceInfo", "RunControlPanel", "Initialize", "CreateEffect",
        "EnumEffects", "GetEffectInfo", "GetForceFeedbackState", "SendForceFeedbackCommand",
        "EnumCreatedEffectObjects", "Escape", "Poll", "SendDeviceData", "EnumEffectsInFile",
        "WriteEffectToFile", "BuildActionMap", "SetActionMap", "GetImageInfo"};
    static char text[64];
    if (id == kIoGetMessage) return "GetMessageA";
    if (id == kIoDinput8Create) return "DirectInput8Create";
    if (id >= kIoDevice) {
        unsigned slot = (id - kIoDevice) % kIoDevSlots;
        unsigned role = (id - kIoDevice) / kIoDevSlots;
        if (slot < sizeof kMethods / sizeof kMethods[0]) {
            std::snprintf(text, sizeof text, "dinput.%s.%s", io_role(role), kMethods[slot]);
        } else {
            std::snprintf(text, sizeof text, "dinput.%s.dev%u", io_role(role), slot);
        }
        return text;
    }
    if (id >= kIoDinput8) {
        std::snprintf(text, sizeof text, "dinput8.%u", id - kIoDinput8);
        return text;
    }
    return id < sizeof kApis / sizeof kApis[0] ? kApis[id] : "?";
}

DWORD io_now() { return g_api.getTickCount ? g_api.getTickCount() : 0; }

void flush_io_locked() {
    if (!g_log) return;
    for (unsigned i = 0; i < kIoTable; ++i) {
        IoRow& row = g_io_rows[i];
        if (!row.nameid) continue;
        LONG count = row.count;
        if ((unsigned)count == row.printed) continue;
        std::fprintf(g_log, "# io %s %08X %u %u\n", io_name(row.nameid),
                     (unsigned)(uintptr_t)row.site, (unsigned)count, g_io_frame);
        row.printed = (unsigned)count;
    }
    for (unsigned i = 0; i < kHrTable; ++i) {
        HrRow& row = g_hr_rows[i];
        if (!row.used || row.count == row.printed) continue;
        std::fprintf(g_log, "# io hr %s %s %08X %u\n", row.role ? io_role(row.role) : "none",
                     kHrMethodNames[row.method], (unsigned)row.hr, row.count);
        row.printed = row.count;
    }
    if (g_hr_dropped) {
        std::fprintf(g_log, "# io hr dropped %u\n", g_hr_dropped);
        g_hr_dropped = 0;
    }
    if (g_replay) {
        std::fprintf(g_log, "# io clockdigest %016llX reads=%u\n",
                     (unsigned long long)g_clock_digest, g_clock_reads);
    }
    std::fflush(g_log);
    g_io_flushed_at = io_now();
}

char g_path_last[32][MAX_PATH];

void log_path(unsigned id, const char* text) {
    if (!g_io || !g_log || !text || id >= 32) return;
    EnterCriticalSection(&g_lock);
    if (std::strcmp(g_path_last[id], text) != 0) {
        std::snprintf(g_path_last[id], MAX_PATH, "%s", text);
        std::fprintf(g_log, "# io path %s %s\n", io_name(id), text);
        std::fflush(g_log);
    }
    LeaveCriticalSection(&g_lock);
}

extern "C" void log_io_call(unsigned nameid, void* site) {
    if (!g_io || !g_log) return;
    unsigned mask = kIoTable - 1;
    unsigned h = ((nameid * 0x9e3779b1u) ^ (unsigned)(uintptr_t)site * 0x85ebca6bu) & mask;
    bool counted = false;
    for (unsigned probes = 0; probes < kIoTable && !counted; ++probes) {
        IoRow& row = g_io_rows[h];
        unsigned n = row.nameid;
        if (!n) break;
        if (n == nameid && row.site == site) {
            InterlockedIncrement(&row.count);
            counted = true;
        }
        h = (h + 1) & mask;
    }
    if (!counted) {
        EnterCriticalSection(&g_lock);
        for (unsigned probes = 0; probes < kIoTable; ++probes) {
            IoRow& row = g_io_rows[h];
            if (row.nameid == nameid && row.site == site) {
                InterlockedIncrement(&row.count);
                break;
            }
            if (!row.nameid) {
                row.site = site;
                row.count = 1;
                row.printed = 0;
                row.nameid = nameid;
                break;
            }
            h = (h + 1) & mask;
        }
        LeaveCriticalSection(&g_lock);
    }
    DWORD now = io_now();
    if (now - g_io_flushed_at >= 1000) {
        EnterCriticalSection(&g_lock);
        if (io_now() - g_io_flushed_at >= 1000) flush_io_locked();
        LeaveCriticalSection(&g_lock);
    }
}

struct DeviceWrap {
    void** vtbl;
    void* real;
    volatile unsigned role;
    unsigned synthetic;
};

extern "C" void log_io_dev(DeviceWrap* device, unsigned slot, void* site) {
    log_io_call(kIoDevice + device->role * kIoDevSlots + slot, site);
}

constexpr uintptr_t kMouseManagerRva = 0x5fefc4;

void read_cursor() {
    static bool broken;
    static bool readable;
    static unsigned first_frame;
    static unsigned last_frame;
    static bool have_last;
    static uint32_t last[7];
    if (broken) return;
    uint32_t state[7] = {};
    bool ok = false;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));
        auto* manager = *reinterpret_cast<unsigned char**>(base + kMouseManagerRva);
        if (manager) {
            std::memcpy(&state[0], manager + 0x58, 4);
            std::memcpy(&state[1], manager + 0x5c, 4);
            std::memcpy(&state[2], manager + 0x74, 4);
            unsigned buttons = 0;
            for (unsigned i = 0; i < 4; ++i) {
                buttons |= (unsigned)(unsigned char)manager[0x70 + i] << (8 * i);
            }
            state[3] = buttons;
            state[4] = (unsigned char)manager[0x54];
            std::memcpy(&state[5], manager + 0x04, 4);
            std::memcpy(&state[6], manager + 0x08, 4);
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    float scale = 0.0f;
    if (ok) std::memcpy(&scale, &state[2], 4);
    if (!ok || !(scale > 0.0f) || !(scale < 1.0e6f)) {
        broken = true;
        if (g_log) {
            EnterCriticalSection(&g_lock);
            std::fprintf(g_log, "# io cursor unreadable\n");
            std::fflush(g_log);
            LeaveCriticalSection(&g_lock);
        }
        return;
    }
    if (!readable) {
        readable = true;
        first_frame = g_io_frame;
    }
    bool window = g_io_frame >= first_frame && g_io_frame - first_frame < 30 &&
                  g_io_frame != last_frame;
    if (window) last_frame = g_io_frame;
    if (!window && have_last && !std::memcmp(last, state, sizeof state)) return;
    have_last = true;
    std::memcpy(last, state, sizeof state);
    if (!g_log) return;
    EnterCriticalSection(&g_lock);
    std::fprintf(g_log, "# io cursor %u %d %d %08X %08X inact=%02X mouse=%08X kbd=%08X\n",
                 g_io_frame, (int32_t)state[0], (int32_t)state[1], state[2], state[3],
                 state[4], state[5], state[6]);
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

unsigned char replay_inact() {
    unsigned char value = 0xff;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));
        auto* manager = *reinterpret_cast<unsigned char**>(base + kMouseManagerRva);
        if (manager) value = manager[0x54];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0xff;
    }
    return value;
}

LPARAM scaled_point(LPARAM lparam) {
    if (g_faithful || !g_scaled || g_scale_factor <= 1) return lparam;
    long x = (long)(short)LOWORD(lparam) * (long)g_scale_factor;
    long y = (long)(short)HIWORD(lparam) * (long)g_scale_factor;
    return (LPARAM)(((unsigned long)(unsigned short)y << 16) | (unsigned short)x);
}

void replay_present() {
    unsigned before = g_activates;
    unsigned first = g_next_event;
    replay::apply_due(g_events, g_event_count, &g_next_event, g_presents, &g_state, &g_activates);
    if (g_log) {
        EnterCriticalSection(&g_lock);
        for (unsigned i = first; i < g_next_event; ++i) {
            const replay::Event& event = g_events[i];
            if (std::strcmp(event.name, "message") != 0 || !g_hwnd || event.nargs < 2) continue;
            LPARAM lparam = (LPARAM)(event.nargs > 2 ? event.arg[2] : 0);
            UINT message = (UINT)event.arg[0];
            if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
                lparam = scaled_point(lparam);
            }
            PostMessageA(g_hwnd, message, (WPARAM)event.arg[1], lparam);
            if (message == WM_MOUSEMOVE) replay::post_add(&g_posted, (unsigned)lparam);
            std::fprintf(g_log, "# replay message frame=%u msg=%04X w=%08X l=%08X\n", g_presents,
                         (unsigned)event.arg[0], (unsigned)event.arg[1], (unsigned)lparam);
        }
        std::fflush(g_log);
        LeaveCriticalSection(&g_lock);
    }
    if (!g_log) return;
    EnterCriticalSection(&g_lock);
    if (g_activates != before && g_probe_stage == 0 && g_hwnd) {
        unsigned char inact = replay_inact();
        PostMessageA(g_hwnd, WM_ACTIVATE, 2, (LPARAM)g_hwnd);
        std::fprintf(g_log, "# replay activate frame=%u hwnd=%08X inact_before=%02X\n", g_presents,
                     (unsigned)(uintptr_t)g_hwnd, inact);
        g_probe_stage = 1;
        g_probe_frame = g_presents;
    } else if (g_probe_stage == 1 && g_presents >= g_probe_frame + 2) {
        unsigned char inact = replay_inact();
        std::fprintf(g_log, "# replay inact frame=%u inact=%02X\n", g_presents, inact);
        if (inact == 1) {
            PostMessageA(g_hwnd, WM_ACTIVATEAPP, 1, (LPARAM)GetCurrentThreadId());
            std::fprintf(g_log, "# replay activateapp frame=%u hwnd=%08X inact=%02X\n",
                         g_presents, (unsigned)(uintptr_t)g_hwnd, inact);
            g_probe_stage = 2;
            g_probe_frame = g_presents;
        } else {
            g_probe_stage = 0;
        }
    } else if (g_probe_stage == 2 && g_presents >= g_probe_frame + 2) {
        std::fprintf(g_log, "# replay inact frame=%u inact=%02X\n", g_presents, replay_inact());
        g_probe_stage = 0;
    }
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

void steer_post(UINT message, WPARAM wparam, long x, long y) {
    LPARAM lparam = (LPARAM)steer::lparam(x, y);
    PostMessageA(g_hwnd, message, wparam, lparam);
    if (g_replay && message == WM_MOUSEMOVE) replay::post_add(&g_posted, (unsigned)lparam);
}

unsigned steer_mask() {
    unsigned mask = 0;
    if (g_replay) {
        if (g_state.keys['A'] || g_state.keys[VK_LEFT]) mask |= steer::kLeft;
        if (g_state.keys['D'] || g_state.keys[VK_RIGHT]) mask |= steer::kRight;
        if (g_state.keys['W'] || g_state.keys[VK_UP]) mask |= steer::kUp;
        if (g_state.keys['S'] || g_state.keys[VK_DOWN]) mask |= steer::kDown;
        return mask;
    }
    if (GetForegroundWindow() != g_hwnd) return 0;
    if ((GetAsyncKeyState('A') | GetAsyncKeyState(VK_LEFT)) & 0x8000) mask |= steer::kLeft;
    if ((GetAsyncKeyState('D') | GetAsyncKeyState(VK_RIGHT)) & 0x8000) mask |= steer::kRight;
    if ((GetAsyncKeyState('W') | GetAsyncKeyState(VK_UP)) & 0x8000) mask |= steer::kUp;
    if ((GetAsyncKeyState('S') | GetAsyncKeyState(VK_DOWN)) & 0x8000) mask |= steer::kDown;
    return mask;
}

void steer_present() {
    if (g_faithful || !g_hwnd) return;
    int dx = 0;
    int dy = 0;
    steer::direction(steer_mask(), &dx, &dy);
    if (!dx && !dy) {
        if (g_steering) {
            g_steering = false;
            steer_post(WM_LBUTTONUP, 0, g_steer_x, g_steer_y);
            if (g_log) {
                EnterCriticalSection(&g_lock);
                std::fprintf(g_log, "# steer end x=%ld y=%ld frame=%u\n", g_steer_x, g_steer_y,
                             g_presents);
                std::fflush(g_log);
                LeaveCriticalSection(&g_lock);
            }
        }
        return;
    }
    RECT client = {};
    bool sized = GetClientRect(g_hwnd, &client) != 0;
    bool first = !g_steering;
    if (first) {
        g_steering = true;
        g_steer_x = sized ? (client.right - client.left) / 2 : 0;
        g_steer_y = sized ? (client.bottom - client.top) / 2 : 0;
        if (g_log) {
            EnterCriticalSection(&g_lock);
            std::fprintf(g_log, "# steer start x=%ld y=%ld frame=%u\n", g_steer_x, g_steer_y,
                         g_presents);
            std::fflush(g_log);
            LeaveCriticalSection(&g_lock);
        }
    }
    steer_post(WM_MOUSEMOVE, 1, g_steer_x, g_steer_y);
    if (first || g_presents % kSteerClickPresents == 0) {
        steer_post(WM_LBUTTONDOWN, 1, g_steer_x, g_steer_y);
        steer_post(WM_LBUTTONUP, 0, g_steer_x, g_steer_y);
    }
    if (sized) {
        long width = client.right - client.left;
        long height = client.bottom - client.top;
        steer::step(&g_steer_x, &g_steer_y, dx, dy, width, height);
    }
}

bool probe_read_dword(uint32_t address, uint32_t* out) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>((uintptr_t)address), 4);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool probe_resolve(const probe::Term& term, uint32_t* address) {

    uint32_t field = term.address;
    uint32_t value = 0;
    if (term.indirect) {
        if (!probe_read_dword(term.address, &value) || !value) return false;
        field = value + term.offset;
    }
    if (!term.has_index) {
        *address = field;
        return true;
    }

    uint32_t array = 0;
    if (!probe_read_dword(field, &array) || !array) return false;
    uint32_t index = 0;
    uint32_t index_field = term.index_address;
    if (term.index_indirect) {
        if (!probe_read_dword(term.index_address, &index) || !index) return false;
        index_field = index + term.index_offset;
    }
    if (!probe_read_dword(index_field, &index) || index > 4999) return false;
    uint32_t element = 0;
    if (!probe_read_dword(array + 4 * index, &element) || !element) return false;
    *address = element + term.field;
    return true;
}

uint32_t probe_read(const probe::Term& term, bool* ok) {
    uint32_t address = 0;
    uint32_t value = 0;
    *ok = probe_resolve(term, &address) && probe_read_dword(address, &value);
    return value;
}

constexpr unsigned kMaxSpawners = 4;
uint32_t g_spawn[kMaxSpawners];
uint32_t g_spawn_owner[kMaxSpawners];
unsigned g_spawn_count;
void spawn_probe_rows();

unsigned g_arm_hits[2];
bool g_arm_on;

void probe_present() {
    if (!g_probe || !g_log) return;
    EnterCriticalSection(&g_lock);
    for (unsigned i = 0; i < g_probe_count; ++i) {
        bool ok = false;
        uint32_t value = probe_read(g_probe_terms[i], &ok);
        if (ok) {
            std::fprintf(g_log, "# probe %s %u %08X\n", g_probe_terms[i].text, g_presents,
                         value);
        } else {
            std::fprintf(g_log, "# probe %s %u unreadable\n", g_probe_terms[i].text,
                         g_presents);
        }
    }
    spawn_probe_rows();
    if (g_arm_on) {
        std::fprintf(g_log, "# probe arm %u 42230D=%u 4AD2E7=%u\n", g_presents, g_arm_hits[0],
                     g_arm_hits[1]);
    }
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

constexpr uint32_t kGameObject = 0x6D09D4;
constexpr uint32_t kSimObject = 0x6D09C0;
constexpr uint32_t kSceneObject = 0x6D15E0;
constexpr uint32_t kArrayOff = 0xEC94;
constexpr uint32_t kHighWaterOff = 0xEC98;
constexpr unsigned kSlots = 5000;

constexpr uint32_t kGlobals[] = {0x6D09C0, 0x6D09D4, 0x6D15E0, 0x6C8910, 0x6D09B4, 0x6D09B8,
                                 0x6D09D8};
constexpr unsigned kCensusStep = 60;
constexpr unsigned kSnapshotGap = 600;
constexpr unsigned kMaxBlobs = 8192;
constexpr size_t kSnapshotArena = 64u << 20;

bool g_census;
unsigned g_census_end;
bool g_census_classes;
unsigned g_census_roots;
bool g_snapshot;
unsigned g_snapshot_at;
census::Class g_class[census::kMaxClasses];
unsigned g_class_count;

struct FieldStat {
    uint32_t slot;
    uint32_t object;
    uint32_t identity[3];
    unsigned char* first;
    unsigned char* prev;
    uint16_t* changes;
    uint16_t* rises;
    uint16_t* drops;
    unsigned samples;
};
FieldStat g_stat[census::kMaxClasses];

constexpr unsigned kUnknownSlots = 64;
struct UnknownShape {
    uint32_t first;
    uint32_t base;
    int offset;
    uint32_t count;
};
UnknownShape g_unknown[kUnknownSlots];
unsigned g_unknown_used;

uint32_t g_slot_live[(kSlots + 31) / 32];
uint32_t g_slot_identity[kSlots];
bool g_slot_history;

bool probe_read_bytes(uint32_t address, void* out, unsigned size) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>((uintptr_t)address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint32_t read_dword_if(uint32_t address) {
    uint32_t value = 0;
    return probe_read_dword(address, &value) ? value : 0;
}

constexpr uint32_t kSpawnVtable = 0x66B8E0;
constexpr uint32_t kSpawnSize = 0x43C;
constexpr uint32_t kSpawnOwnerVtable = 0x66B778;
constexpr uint32_t kSpawnOwnerSize = 0x448;

void find_spawners() {
    if (g_spawn_count) return;
    unsigned char* at = nullptr;
    MEMORY_BASIC_INFORMATION mbi = {};
    while (VirtualQuery(at, &mbi, sizeof mbi) == sizeof mbi && mbi.RegionSize) {
        unsigned char* base = static_cast<unsigned char*>(mbi.BaseAddress);
        at = base + mbi.RegionSize;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE) continue;
        if (!(mbi.Protect & PAGE_READWRITE) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            continue;
        }
        const uint32_t* word = reinterpret_cast<const uint32_t*>(base);
        const uint32_t* end = reinterpret_cast<const uint32_t*>(at);
        for (; word + 0x110 <= end; ++word) {
            if (*word != kSpawnVtable) continue;
            uint32_t owner = word[0x28 / 4];
            if (!owner || read_dword_if(owner) != kSpawnOwnerVtable) continue;
            if (g_spawn_count == kMaxSpawners) break;
            g_spawn[g_spawn_count] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(word));
            g_spawn_owner[g_spawn_count] = owner;
            ++g_spawn_count;
        }
    }
    if (g_log) {
        std::fprintf(g_log, "# spawn scan count=%u", g_spawn_count);
        for (unsigned i = 0; i < g_spawn_count; ++i) {
            std::fprintf(g_log, " e%u=%08X y%u=%08X", i, g_spawn[i], i, g_spawn_owner[i]);
        }
        std::fprintf(g_log, "\n");
    }
}

void spawn_probe_rows() {
    if (!g_spawn_count && g_presents % 60 == 0) find_spawners();
    for (unsigned i = 0; i < g_spawn_count; ++i) {
        const struct {
            const char* name;
            uint32_t base;
            uint32_t offset;
        } rows[] = {{"timer", g_spawn[i], 0x408},          {"burst", g_spawn[i], 0x40C},
                    {"children", g_spawn[i], 0x420},
                    {"spawned", g_spawn_owner[i], 0x60},   {"limit", g_spawn_owner[i], 0x140}};
        for (const auto& row : rows) {
            uint32_t value = 0;
            if (probe_read_dword(row.base + row.offset, &value)) {
                std::fprintf(g_log, "# probe spawn%u.%s %u %08X\n", i, row.name, g_presents,
                             value);
            } else {
                std::fprintf(g_log, "# probe spawn%u.%s %u unreadable\n", i, row.name,
                             g_presents);
            }
        }
    }
}

struct Extra {
    uint32_t vtable;
    uint32_t size;
};
constexpr Extra kExtras[] = {
    {0x669338, 0x0CCC},
    {0x65B8E0, 0x1C10},
    {0x65DBF0, 0x1900},
    {0x656A40, 0x1FB0},
};

void census_classes() {
    if (g_class_count) return;
    g_class_count = census::decode(probe_read_dword, probe_read_bytes, g_class,
                                   census::kMaxClasses);
    for (const Extra& extra : kExtras) {
        if (g_class_count >= census::kMaxClasses) break;
        census::Class& entry = g_class[g_class_count++];
        entry.id = 0xFFFFFFFFu;
        entry.factory = 0;
        entry.size = extra.size;
        entry.vtable = extra.vtable;
    }
}

const census::Class* class_of(uint32_t object) {
    return census::by_vtable(g_class, g_class_count, object ? read_dword_if(object) : 0);
}

unsigned class_index(const census::Class* found) { return (unsigned)(found - g_class); }

const census::Class* base_vtable(uint32_t object, int* offset) {
    for (int back = 4; back <= 0x80; back += 4) {
        const census::Class* found =
            census::by_vtable(g_class, g_class_count, read_dword_if(object - (uint32_t)back));
        if (found) {
            *offset = -back;
            return found;
        }
    }
    *offset = 0;
    return nullptr;
}

uint32_t g_image_low;
uint32_t g_image_high;

void image_span() {
    if (g_image_high) return;
    unsigned char* base = reinterpret_cast<unsigned char*>((uintptr_t)GetModuleHandleA(nullptr));
    g_image_low = (uint32_t)(uintptr_t)base;
    g_image_high = g_image_low + 0x800000;
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const IMAGE_NT_HEADERS* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE) g_image_high = g_image_low + nt->OptionalHeader.SizeOfImage;
    }
}

bool is_image(uint32_t value) {
    image_span();
    return value >= g_image_low && value < g_image_high;
}

const char* target_name(uint32_t value) {
    if (!value) return "null";
    if (is_image(value)) return "image";
    const census::Class* found = class_of(value);
    static char name[32];
    if (found) {
        std::snprintf(name, sizeof name, "id%u", found->id);
        return name;
    }
    return "heap";
}

const char* kind_of(uint32_t first, uint32_t last) {
    uint32_t bits = last ? last : first;
    if (bits >= 0x10000 && bits < 0x7FFF0000 && is_image(bits)) return "ptr";
    uint32_t exponent = (bits >> 23) & 0xFF;
    if (bits && exponent >= 0x60 && exponent <= 0x86) return "float";
    if (bits >= 0x10000 && bits < 0x7FFF0000) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<const void*>((uintptr_t)bits), &mbi, sizeof mbi) ==
                sizeof mbi &&
            mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE) {
            return "ptr";
        }
    }
    return "int";
}

FieldStat* field_stat(unsigned index) {
    FieldStat& s = g_stat[index];
    if (s.first) return &s;
    uint32_t size = g_class[index].size & ~3u;
    if (!size) return nullptr;
    size_t words = size / 4;
    s.first = static_cast<unsigned char*>(std::calloc(size, 1));
    s.prev = static_cast<unsigned char*>(std::calloc(size, 1));
    s.changes = static_cast<uint16_t*>(std::calloc(words, sizeof(uint16_t)));
    s.rises = static_cast<uint16_t*>(std::calloc(words, sizeof(uint16_t)));
    s.drops = static_cast<uint16_t*>(std::calloc(words, sizeof(uint16_t)));
    if (!s.first || !s.prev || !s.changes || !s.rises || !s.drops) return nullptr;
    s.slot = 0xFFFFFFFFu;
    return &s;
}

void census_diff(unsigned index, FieldStat* s, uint32_t object) {
    uint32_t identity[3] = {};
    bool live = probe_read_dword(object + 8, &identity[0]) &&
                probe_read_dword(object + 0xC, &identity[1]) &&
                probe_read_dword(object + 0x10, &identity[2]);
    if (s->object != object || !live || std::memcmp(identity, s->identity, sizeof identity) != 0) {
        s->object = object;
        std::memcpy(s->identity, identity, sizeof identity);
        s->samples = 0;
    }
    uint32_t size = g_class[index].size & ~3u;
    for (uint32_t off = 0; off < size; off += 4) {
        uint32_t value = 0;
        bool ok = probe_read_dword(object + off, &value);
        if (!s->samples) {
            if (ok) std::memcpy(s->first + off, &value, 4);
            if (ok) std::memcpy(s->prev + off, &value, 4);
            continue;
        }
        if (!ok) continue;
        uint32_t previous = 0;
        std::memcpy(&previous, s->prev + off, 4);
        size_t word = off / 4;
        if (value != previous) {
            if (s->changes[word] < 0xFFFF) ++s->changes[word];

            if (value > previous && value - previous <= 600 && s->rises[word] < 0xFFFF) {
                ++s->rises[word];
            } else if (previous && value < previous && s->drops[word] < 0xFFFF) {
                ++s->drops[word];
            }
        }
        std::memcpy(s->prev + off, &value, 4);
    }
    ++s->samples;
}

void census_summary(unsigned frame) {
    for (unsigned i = 0; i < g_class_count; ++i) {
        FieldStat& s = g_stat[i];
        if (!s.samples) continue;
        uint32_t size = g_class[i].size & ~3u;
        unsigned changed = 0;
        for (uint32_t off = 0; off < size; off += 4) {
            if (s.changes[off / 4]) ++changed;
        }
        std::fprintf(g_log, "# census class frame=%u id=%d vtable=%08X size=%04X factory=%08X "
                           "samples=%u changed=%u\n",
                     frame, (int)g_class[i].id, g_class[i].vtable, g_class[i].size,
                     g_class[i].factory, s.samples, changed);
        unsigned rows = 0;
        for (uint32_t off = 0; off < size; off += 4) {
            size_t word = off / 4;
            if (!s.changes[word]) continue;
            uint32_t first = 0, last = 0;
            std::memcpy(&first, s.first + off, 4);
            std::memcpy(&last, s.prev + off, 4);
            if (rows++ >= 512) continue;
            std::fprintf(g_log,
                         "# census field id=%d off=%04X changes=%u kind=%s target=%s "
                         "first=%08X last=%08X\n",
                         (int)g_class[i].id, off, s.changes[word], kind_of(first, last),
                         target_name(last ? last : (first >= 0x10000 ? first : 0)), first, last);
        }
        for (uint32_t off = 0; off < size; off += 4) {
            size_t word = off / 4;
            if (s.rises[word] < 20) continue;
            uint32_t first = 0, last = 0;
            std::memcpy(&first, s.first + off, 4);
            std::memcpy(&last, s.prev + off, 4);
            std::fprintf(g_log, "# census counter id=%d off=%04X rises=%u drops=%u first=%08X "
                               "last=%08X\n",
                         (int)g_class[i].id, off, s.rises[word], s.drops[word], first, last);
        }
    }
    for (unsigned k = 0; k < g_unknown_used; ++k) {
        std::fprintf(g_log, "# census unknown first=%08X count=%u image=%u base=%08X offset=%d\n",
                     g_unknown[k].first, g_unknown[k].count,
                     is_image(g_unknown[k].first) ? 1u : 0u, g_unknown[k].base,
                     g_unknown[k].offset);
    }
}

void census_ring(unsigned frame) {
    uint32_t game = read_dword_if(kGameObject);
    uint32_t scene = read_dword_if(kSceneObject);
    uint32_t array = scene ? read_dword_if(scene + kArrayOff) : 0;
    uint32_t index = game ? read_dword_if(game + 0x1E74) : 0xFFFFFFFFu;
    uint32_t tornado = (array && index < kSlots) ? read_dword_if(array + 4 * index) : 0;
    if (!tornado) {
        std::fprintf(g_log, "# census ring frame=%u unreadable\n", frame);
        return;
    }
    uint32_t cursor = read_dword_if(tornado + 0xFDC);
    uint32_t count = read_dword_if(tornado + 0xFE0);
    uint32_t head = read_dword_if(tornado + 0xFA8);
    std::fprintf(g_log, "# census ring frame=%u obj=%08X cursor=%u count=%u head=%08X\n", frame,
                 tornado, cursor, count, head);
    static uint32_t last_count = 0xFFFFFFFFu;
    if (!head || count == last_count) return;
    last_count = count;
    uint32_t node = head;
    {
        uint32_t word[5] = {};
        if (!probe_read_bytes(node, word, sizeof word)) return;
        std::fprintf(g_log, "# census ring node frame=%u addr=%08X words=%08X %08X %08X %08X "
                           "%08X\n",
                     frame, node, word[0], word[1], word[2], word[3], word[4]);
        for (unsigned f = 0; f < 5; ++f) {
            uint32_t to = word[f];
            if (to < 0x10000 || to >= 0x7FFF0000) continue;
            MEMORY_BASIC_INFORMATION mbi = {};
            if (VirtualQuery(reinterpret_cast<const void*>((uintptr_t)to), &mbi, sizeof mbi) !=
                sizeof mbi) {
                continue;
            }
            if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE) continue;
            std::fprintf(g_log, "# census ring link frame=%u from=%08X field=%u to=%08X\n", frame,
                         node, f, to);
        }
    }
}

void census_present() {
    if (!g_census || !g_log || g_presents > g_census_end) return;
    const bool last = g_presents >= g_census_end;
    if (!last && g_presents % kCensusStep) return;
    census_classes();
    EnterCriticalSection(&g_lock);
    uint32_t scene = read_dword_if(kSceneObject);
    uint32_t array = scene ? read_dword_if(scene + kArrayOff) : 0;
    uint32_t high = scene ? read_dword_if(scene + kHighWaterOff) : 0;
    if (g_class_count && !g_census_classes) {
        g_census_classes = true;
        for (unsigned i = 0; i < g_class_count; ++i) {
            std::fprintf(g_log, "# census class id=%d factory=%08X size=%04X vtable=%08X\n",
                         (int)g_class[i].id, g_class[i].factory, g_class[i].size, g_class[i].vtable);
        }
    }
    if (g_census_roots != 7) {
        const uint32_t roots[3] = {kGameObject, kSceneObject, kSimObject};
        const char* const names[3] = {"game", "scene", "sim"};
        for (unsigned r = 0; r < 3; ++r) {
            if (g_census_roots & (1u << r)) continue;
            uint32_t address = read_dword_if(roots[r]);
            if (!address) continue;
            g_census_roots |= 1u << r;
            const census::Class* found = class_of(address);
            std::fprintf(g_log, "# census root name=%s addr=%08X vtable=%08X class=%d size=%04X "
                               "window=%04X frame=%u\n",
                         names[r], address, read_dword_if(address), found ? (int)found->id : -1,
                         found ? found->size : 0,
                         found ? found->size : (r == 0 ? 0x2000 : r == 1 ? 0xECA4 : 0x200),
                         g_presents);
        }
    }
    unsigned counts[census::kMaxClasses] = {};
    uint32_t sample[census::kMaxClasses] = {};
    unsigned live = 0, unknown = 0;
    unsigned frees = 0, reused = 0;
    for (unsigned i = 0; i < kSlots; ++i) {
        uint32_t object = array ? read_dword_if(array + 4 * i) : 0;
        bool was = (g_slot_live[i / 32] >> (i % 32)) & 1u;
        uint32_t identity = object ? read_dword_if(object + 8) ^ read_dword_if(object + 0xC) ^
                                         read_dword_if(object + 0x10)
                                   : 0;
        if (g_slot_history && was && !object) {
            if (++frees <= 16) {
                std::fprintf(g_log, "# census free frame=%u slot=%u\n", g_presents, i);
            }
        } else if (g_slot_history && was && object && identity != g_slot_identity[i]) {
            if (++reused <= 16) {
                std::fprintf(g_log, "# census reuse frame=%u slot=%u\n", g_presents, i);
            }
        }
        if (object) g_slot_live[i / 32] |= 1u << (i % 32);
        else g_slot_live[i / 32] &= ~(1u << (i % 32));
        g_slot_identity[i] = identity;
        if (!object) continue;
        ++live;
        const census::Class* found = class_of(object);
        if (!found) {
            ++unknown;
            uint32_t first = read_dword_if(object);
            int offset = 0;
            const census::Class* base = base_vtable(object, &offset);
            unsigned k = 0;
            for (; k < g_unknown_used; ++k) {
                if (g_unknown[k].first == first && g_unknown[k].offset == offset) break;
            }
            if (k == g_unknown_used && g_unknown_used < kUnknownSlots) {
                g_unknown[g_unknown_used] = {first, base ? base->vtable : 0, offset, 0};
                ++g_unknown_used;
            }
            if (k < kUnknownSlots) ++g_unknown[k].count;
            continue;
        }
        unsigned index = class_index(found);
        ++counts[index];
        FieldStat* s = field_stat(index);
        if (!s) continue;
        if (s->slot == 0xFFFFFFFFu) s->slot = i;
        if (s->slot == i) sample[index] = object;
    }
    for (unsigned i = 0; i < g_class_count; ++i) {
        if (sample[i]) census_diff(i, &g_stat[i], sample[i]);
    }
    std::fprintf(g_log,
                 "# census frame=%u high=%u live=%u unknown=%u classes=%u frees=%u reused=%u\n",
                 g_presents, high, live, unknown, g_class_count, frees, reused);
    g_slot_history = true;
    for (unsigned i = 0; i < g_class_count; ++i) {
        if (!counts[i]) continue;
        std::fprintf(g_log, "# census count frame=%u id=%d vtable=%08X n=%u\n", g_presents,
                     (int)g_class[i].id, g_class[i].vtable, counts[i]);
    }
    census_ring(g_presents);
    if (last) census_summary(g_presents);
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

struct Blob {
    uint32_t address;
    uint32_t size;
    unsigned char* data;
    bool owned;
    bool object;

    uint32_t guard_address;
    uint32_t guard_value;
};
Blob* g_blobs;
unsigned g_blob_count;
unsigned char* g_arena;
size_t g_arena_used;
bool g_snap_taken;
bool g_snap_overflow;
uint64_t g_snap_heap_total;
uint64_t g_snap_image_total;
uint32_t g_snap_hash[2];

bool frame_hash(uint32_t* hash);

bool committed(uint32_t address, unsigned size, bool writable) {
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(reinterpret_cast<const void*>((uintptr_t)address), &mbi, sizeof mbi) !=
        sizeof mbi) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    if (writable && !(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE))) {
        return false;
    }
    return (uintptr_t)address + size <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

uint64_t private_bytes() {
    uint64_t total = 0;
    unsigned char* at = nullptr;
    MEMORY_BASIC_INFORMATION mbi = {};
    while (VirtualQuery(at, &mbi, sizeof mbi) == sizeof mbi && mbi.RegionSize) {
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect & PAGE_READWRITE)) {
            total += mbi.RegionSize;
        }
        at += mbi.RegionSize;
    }
    return total;
}

int blob_add_one(uint32_t address, uint32_t size, bool owned, bool object) {
    if (address < 0x10000 || !size || size > 0x200000) return -1;
    for (unsigned i = 0; i < g_blob_count; ++i) {
        if (g_blobs[i].address == address) {
            if (owned) g_blobs[i].owned = true;
            if (object) g_blobs[i].object = true;
            return (int)i;
        }
    }
    if (g_blob_count >= kMaxBlobs || g_arena_used + size > kSnapshotArena) {
        g_snap_overflow = true;
        return -1;
    }
    Blob& blob = g_blobs[g_blob_count];
    blob.address = address;
    blob.size = size;
    blob.data = g_arena + g_arena_used;
    blob.owned = owned;
    blob.object = object;
    std::memcpy(blob.data, reinterpret_cast<const void*>((uintptr_t)address), size);
    g_arena_used += size;
    return (int)g_blob_count++;
}

int blob_add(uint32_t address, uint32_t size, bool writable, bool owned, bool object = false) {
    int first = -1;
    uint32_t end = address + size;
    for (uint32_t at = address; at < end;) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<const void*>((uintptr_t)at), &mbi, sizeof mbi) !=
            sizeof mbi) {
            break;
        }
        uint32_t region_end = (uint32_t)(uintptr_t)mbi.BaseAddress + (uint32_t)mbi.RegionSize;
        uint32_t chunk_end = region_end < end ? region_end : end;
        bool usable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        if (usable && writable) {
            usable = (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) != 0;
        }
        if (usable && chunk_end > at) {
            int index = blob_add_one(at, chunk_end - at, owned, object);
            if (first < 0) first = index;
        }
        at = chunk_end;
    }
    return first;
}

int blob_add_field(uint32_t object, uint32_t vtable, uint32_t offset, uint32_t size) {
    int index = blob_add(object + offset, size, true, false);
    if (index >= 0) {
        g_blobs[index].guard_address = object;
        g_blobs[index].guard_value = vtable;
    }
    return index;
}

int blob_add_root(uint32_t address, uint32_t window, const char* name) {
    int index = blob_add(address, window, true, false, true);
    uint32_t size = window;
    const census::Class* found = class_of(address);
    if (found) {
        size = found->size;
        int sized = blob_add(address, size, true, false, true);
        if (sized >= 0) index = sized;
    }
    if (g_log) {
        std::fprintf(g_log, "# snapshot root name=%s addr=%08X size=%04X class=%d\n", name,
                     address, size, found ? (int)found->id : -1);
    }
    return index;
}

void snapshot_take(unsigned frame) {
    census_classes();
    g_snap_heap_total = private_bytes();
    g_arena = static_cast<unsigned char*>(std::calloc(1, kSnapshotArena));
    g_blobs = static_cast<Blob*>(std::calloc(kMaxBlobs, sizeof(Blob)));
    if (!g_arena || !g_blobs) {
        if (g_log) {
            std::fprintf(g_log, "# snapshot failed (no memory)\n");
            std::fflush(g_log);
        }
        return;
    }
    unsigned char* base = reinterpret_cast<unsigned char*>((uintptr_t)GetModuleHandleA(nullptr));
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const IMAGE_NT_HEADERS* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE) {

            const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
                if (!(section[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
                uint32_t va = (uint32_t)(uintptr_t)(base + section[i].VirtualAddress);
                uint32_t size = section[i].Misc.VirtualSize ? section[i].Misc.VirtualSize
                                                           : section[i].SizeOfRawData;
                size = (size + 3) & ~3u;
                g_snap_image_total += size;
            }
        }
    }

    find_spawners();
    for (unsigned i = 0; i < g_spawn_count; ++i) {
        uint32_t e = g_spawn[i];
        uint32_t y = g_spawn_owner[i];
        blob_add_field(e, kSpawnVtable, 0x404, 0x1C);
        blob_add_field(e, kSpawnVtable, 0x428, 0x0C);
        blob_add_field(y, kSpawnOwnerVtable, 0x60, 0x04);
        blob_add_field(y, kSpawnOwnerVtable, 0x7C, 0x08);
        blob_add_field(y, kSpawnOwnerVtable, 0x138, 0x14);
    }

    for (uint32_t global : kGlobals) blob_add(global, 4, true, false);
    uint32_t game = read_dword_if(kGameObject);
    uint32_t scene = read_dword_if(kSceneObject);
    uint32_t sim = read_dword_if(kSimObject);
    unsigned actors = 0;
    if (game) blob_add_root(game, 0x2000, "game");
    if (sim) blob_add_root(sim, 0x200, "sim");
    if (scene) blob_add_root(scene, 0xECA4, "scene");
    uint32_t array = scene ? read_dword_if(scene + kArrayOff) : 0;
    if (array) blob_add(array, kSlots * 4, true, false);
    for (unsigned i = 0; i < kSlots; ++i) {
        uint32_t object = array ? read_dword_if(array + 4 * i) : 0;
        if (!object) continue;
        const census::Class* found = class_of(object);
        uint32_t begin = object;
        if (!found) {

            int offset = 0;
            found = base_vtable(object, &offset);
            if (!found) continue;
            begin = object + (uint32_t)offset;
        }
        unsigned before = g_blob_count;
        if (blob_add(begin, found->size, true, true, true) >= 0 && g_blob_count > before) ++actors;
    }

    unsigned owned = 0;
    for (unsigned i = 0; i < g_blob_count; ++i) {
        if (!g_blobs[i].owned) continue;
        for (uint32_t off = 0; off + 4 <= g_blobs[i].size; off += 4) {
            uint32_t value = 0;
            std::memcpy(&value, g_blobs[i].data + off, 4);
            if (value < 0x10000 || value >= 0x7FFF0000) continue;
            const census::Class* found = class_of(value);
            if (!found) continue;
            unsigned before = g_blob_count;
            if (blob_add(value, found->size, true, true) >= 0 && g_blob_count > before) ++owned;
        }
    }

    unsigned ring = 0;
    if (array && game) {
        uint32_t index = read_dword_if(game + 0x1E74);
        uint32_t tornado = index < kSlots ? read_dword_if(array + 4 * index) : 0;
        uint32_t head = tornado ? read_dword_if(tornado + 0xFA8) : 0;
        uint32_t count = tornado ? read_dword_if(tornado + 0xFE0) : 0;
        if (head && count <= 6) {
            ring = count ? count : 1;
            blob_add(head, ring * 0x14, true, false);
            if (g_log) {
                std::fprintf(g_log, "# snapshot ring head=%08X nodes=%u\n", head, ring);
            }
        }
    }

    unsigned tor_fields = 0, tor_sized = 0;
    unsigned long long tor_bytes = 0;
    uint32_t tor_min = 0xFFFFFFFFu, tor_max = 0;
    if (array && game) {
        uint32_t index = read_dword_if(game + 0x1E74);
        uint32_t tornado = index < kSlots ? read_dword_if(array + 4 * index) : 0;
        const census::Class* found = tornado ? class_of(tornado) : nullptr;
        if (found) {
            HANDLE heap = GetProcessHeap();
            for (uint32_t off = 0; off + 4 <= found->size; off += 4) {
                uint32_t value = read_dword_if(tornado + off);
                if (value < 0x10000 || value >= 0x7FFF0000) continue;
                SIZE_T size = (SIZE_T)-1;
                __try {
                    size = HeapSize(heap, 0, reinterpret_cast<LPCVOID>((uintptr_t)value));
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    size = (SIZE_T)-1;
                }
                if (size == (SIZE_T)-1 || !size) continue;
                ++tor_fields;
                ++tor_sized;
                tor_bytes += size;
                if (size < tor_min) tor_min = (uint32_t)size;
                if (size > tor_max) tor_max = (uint32_t)size;
            }
        }
    }
    if (g_log) {
        std::fprintf(g_log,
                     "# snapshot tornado pointers=%u sized=%u bytes=%llu min=%u max=%u\n",
                     tor_fields, tor_sized, tor_bytes, tor_min == 0xFFFFFFFFu ? 0 : tor_min,
                     tor_max);
        std::fprintf(g_log,
                     "# snapshot take frame=%u blobs=%u bytes=%zu image=%llu actors=%u "
                     "owned=%u heap=%llu overflow=%u\n",
                     frame, g_blob_count, g_arena_used, (unsigned long long)g_snap_image_total,
                     actors, owned, (unsigned long long)g_snap_heap_total,
                     g_snap_overflow ? 1 : 0);
        std::fflush(g_log);
    }
    g_snap_taken = true;
}

void snapshot_restore(unsigned frame) {
    unsigned written = 0, skipped = 0, reused = 0;
    size_t bytes = 0;
    for (unsigned i = 0; i < g_blob_count; ++i) {
        Blob& blob = g_blobs[i];
        if (!committed(blob.address, blob.size, true)) {
            ++skipped;
            continue;
        }
        if (blob.object) {

            uint32_t now = 0, then = 0;
            std::memcpy(&now, reinterpret_cast<const void*>((uintptr_t)blob.address), 4);
            std::memcpy(&then, blob.data, 4);
            if (now != then) {
                ++skipped;
                ++reused;
                continue;
            }
        }
        if (blob.guard_address && read_dword_if(blob.guard_address) != blob.guard_value) {
            ++skipped;
            ++reused;
            continue;
        }
        std::memcpy(reinterpret_cast<void*>((uintptr_t)blob.address), blob.data, blob.size);
        bytes += blob.size;
        ++written;
    }
    if (g_log) {
        std::fprintf(g_log,
                     "# snapshot restore frame=%u blobs=%u bytes=%zu skipped=%u reused=%u\n",
                     frame, written, bytes, skipped, reused);
        std::fflush(g_log);
    }
}

void snapshot_present() {
    if (!g_snapshot || !g_log) return;
    if (!g_snap_taken) {
        if (g_presents != g_snapshot_at) return;
        uint32_t hash = 0;
        snapshot_take(g_presents);
        if (frame_hash(&hash) && g_log) {
            std::fprintf(g_log, "# snapshot frame frame=%u hash=%08X pass=first\n", g_presents,
                         hash);
        }
        std::fflush(g_log);
        return;
    }
    if (g_presents == g_snapshot_at + kSnapshotGap) {
        uint32_t hash = 0;
        if (frame_hash(&hash)) g_snap_hash[0] = hash;
        std::fprintf(g_log, "# snapshot frame frame=%u hash=%08X pass=first\n", g_presents,
                     g_snap_hash[0]);
        std::fflush(g_log);
        snapshot_restore(g_presents);
    } else if (g_presents == g_snapshot_at + 2 * kSnapshotGap) {
        uint32_t hash = 0;
        if (frame_hash(&hash)) g_snap_hash[1] = hash;
        std::fprintf(g_log, "# snapshot frame frame=%u hash=%08X pass=second\n", g_presents,
                     g_snap_hash[1]);
        std::fflush(g_log);
    }
}

uint32_t image_va(uint32_t va);

constexpr uint32_t kRandGlobal = 0x6C8910;
constexpr uint32_t kFreeStackOff = 0xEC9C;
constexpr uint32_t kFreeCountOff = 0xECA0;
constexpr uint32_t kRingHeadOff = 0xFA8;
constexpr uint32_t kRingCountOff = 0xFE0;
constexpr uint32_t kRingNodeBytes = 0x14;
constexpr uint32_t kChildHeadOff = 0x424;
constexpr uint32_t kChildCountOff = 0x420;
constexpr uint32_t kChildNext = 0x94;
constexpr uint32_t kChildPrev = 0x98;
constexpr uint32_t kSpawnChildSize = 0x9C;
constexpr unsigned kSaveChildren = 256;
constexpr uint64_t kSaveBlockBytes = 64u << 20;
constexpr uint32_t kKindActor = 1;
constexpr uint32_t kKindRoot = 2;
constexpr uint32_t kKindBlock = 3;
constexpr uint32_t kKindSpawner = 4;
constexpr uint32_t kKindOwner = 5;
constexpr uint32_t kKindChild = 6;
constexpr unsigned kSaveRecords =
    kSlots + 4 * (3 + kSaveChildren) + 3 + save::RefTable::kMaxBlocks;

constexpr uint32_t kRootGlobal[3] = {kGameObject, kSceneObject, kSimObject};
constexpr uint32_t kRootSize[3] = {0x1FB0, 0xECA4, 0x200};
constexpr const char* kRootName[3] = {"game", "scene", "sim"};
constexpr unsigned kLevelHigh = 1000;

static_assert(sizeof(save::Header) == save::kHeaderBytes, "header is 64 bytes");
static_assert(sizeof(save::Section) == save::kSectionEntryBytes, "directory entry is 12 bytes");
static_assert(sizeof(save::Patch) == 12, "patch is 12 bytes");
static_assert(save::kSectionCount == 8, "eight sections");

struct SaveRec {
    uint32_t id;
    uint32_t kind;
    uint32_t class_id;
    uint32_t size;
    uint32_t tag;
    uint32_t address;
    uint32_t patch_count;
    uint32_t patch_cap;
    unsigned char* body;
    save::Patch* patch;
};

bool g_save;
bool g_save_done;
unsigned g_save_at;
char g_save_path[MAX_PATH * 2];
bool g_load;
bool g_load_done;
unsigned g_load_stop;
unsigned g_load_mask = 7;
unsigned g_load_root = 7;
unsigned g_load_at;
unsigned g_load_bytes;
bool g_save_raw;
unsigned g_load_actors;
char g_load_path[MAX_PATH * 2];
SaveRec* g_srec;
unsigned g_srec_count;
unsigned g_block_first;
unsigned g_save_unknown;
unsigned g_save_unres;
unsigned g_save_nameonly;
unsigned g_save_overflow;
uint64_t g_save_block_bytes;

uint32_t g_save_tornado;
unsigned g_save_excl_other;
uint64_t g_save_excl_other_bytes;
unsigned g_save_excl_tornado;
uint64_t g_save_excl_tornado_bytes;
unsigned g_save_excl_capped;
uint64_t g_save_excl_capped_bytes;
unsigned g_save_excl_big;
uint32_t g_save_excl_addr[8192];
unsigned g_save_excl_count;

void save_row(const char* text, ...) {
    if (!g_log) return;
    va_list args;
    va_start(args, text);
    EnterCriticalSection(&g_lock);
    std::vfprintf(g_log, text, args);
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
    va_end(args);
}

SIZE_T heap_block(uint32_t address) {
    SIZE_T size = (SIZE_T)-1;
    __try {
        size = HeapSize(GetProcessHeap(), 0, reinterpret_cast<LPCVOID>((uintptr_t)address));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        size = (SIZE_T)-1;
    }
    return size;
}

constexpr unsigned kMaxRegions = 1024;
struct Region {
    uint32_t low;
    uint32_t high;
};
Region g_region[kMaxRegions];
unsigned g_region_count;

void scan_regions() {
    g_region_count = 0;
    unsigned char* at = nullptr;
    MEMORY_BASIC_INFORMATION mbi = {};
    while (g_region_count < kMaxRegions && VirtualQuery(at, &mbi, sizeof mbi) == sizeof mbi &&
           mbi.RegionSize) {
        unsigned char* base = static_cast<unsigned char*>(mbi.BaseAddress);
        at = base + mbi.RegionSize;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE) continue;
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) continue;
        if (!(mbi.Protect & PAGE_READWRITE)) continue;
        g_region[g_region_count].low = (uint32_t)(uintptr_t)base;
        g_region[g_region_count].high = (uint32_t)(uintptr_t)at;
        ++g_region_count;
    }
}

bool private_rw(uint32_t address) {
    unsigned low = 0;
    unsigned high = g_region_count;
    while (low < high) {
        unsigned mid = low + (high - low) / 2;
        if (address < g_region[mid].low) {
            high = mid;
        } else if (address >= g_region[mid].high) {
            low = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

void write_dword(uint32_t address, uint32_t value) {
    __try {
        std::memcpy(reinterpret_cast<void*>((uintptr_t)address), &value, 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

uint32_t alloc_object(uint32_t size) {
    typedef void*(__cdecl * Fn)(uint32_t);
    uint32_t at = 0;
    __try {
        at = (uint32_t)(uintptr_t)((Fn)image_va(census::kAllocator))(size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        at = 0;
    }
    return at;
}

void image_id(unsigned char* out) {
    std::memset(out, 0, 32);
    const unsigned char* base =
        reinterpret_cast<const unsigned char*>((uintptr_t)GetModuleHandleA(nullptr));
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const uint32_t words[4] = {nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage,
                               nt->OptionalHeader.CheckSum,
                               nt->OptionalHeader.AddressOfEntryPoint};
    std::memcpy(out, words, sizeof words);
}

bool under_game(const char* path) {
    char full[MAX_PATH * 2] = {};
    char exe[MAX_PATH] = {};
    if (!GetFullPathNameA(path, sizeof full, full, nullptr)) return false;
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char* slash = std::strrchr(exe, '\\');
    if (!slash) return false;
    slash[1] = 0;
    return _strnicmp(full, exe, std::strlen(exe)) == 0;
}

int save_rec(uint32_t tag, uint32_t kind, uint32_t class_id, uint32_t address, uint32_t size) {
    if (!size || g_srec_count >= kSaveRecords) return -1;
    unsigned char* body = static_cast<unsigned char*>(std::malloc(size));
    if (!body) return -1;
    if (!probe_read_bytes(address, body, size)) {
        std::free(body);
        return -1;
    }
    SaveRec& rec = g_srec[g_srec_count];
    rec.id = g_srec_count + 1;
    rec.kind = kind;
    rec.class_id = class_id;
    rec.size = size;
    rec.tag = tag;
    rec.address = address;
    rec.patch_count = 0;
    rec.patch_cap = 0;
    rec.body = body;
    rec.patch = nullptr;
    return (int)g_srec_count++;
}

bool save_patch(SaveRec& rec, uint32_t offset, uint32_t id) {
    if (rec.patch_count == rec.patch_cap) {
        uint32_t cap = rec.patch_cap ? rec.patch_cap * 2 : 8;
        save::Patch* grown =
            static_cast<save::Patch*>(std::realloc(rec.patch, (size_t)cap * sizeof(save::Patch)));
        if (!grown) return false;
        rec.patch = grown;
        rec.patch_cap = cap;
    }
    save::Patch& patch = rec.patch[rec.patch_count++];
    patch.offset = offset;
    patch.block = id;
    patch.at = 0;
    return true;
}

void save_clear() {
    for (unsigned i = 0; i < g_srec_count; ++i) {
        std::free(g_srec[i].body);
        std::free(g_srec[i].patch);
    }
    std::free(g_srec);
    g_srec = nullptr;
    g_srec_count = 0;
    g_block_first = 0;
    g_save_unknown = 0;
    g_save_unres = 0;
    g_save_nameonly = 0;
    g_save_overflow = 0;
    g_save_block_bytes = 0;
    g_save_tornado = 0;
    g_save_excl_other = 0;
    g_save_excl_other_bytes = 0;
    g_save_excl_tornado = 0;
    g_save_excl_tornado_bytes = 0;
    g_save_excl_capped = 0;
    g_save_excl_capped_bytes = 0;
    g_save_excl_big = 0;
    g_save_excl_count = 0;
}

struct ObjectId {
    uint32_t address;
    uint32_t id;
};
ObjectId* g_oid;
unsigned g_oid_count;

int object_id_cmp(const void* a, const void* b) {
    uint32_t x = static_cast<const ObjectId*>(a)->address;
    uint32_t y = static_cast<const ObjectId*>(b)->address;
    return x < y ? -1 : (x > y ? 1 : 0);
}

uint32_t save_object(uint32_t address) {
    unsigned low = 0;
    unsigned high = g_oid_count;
    while (low < high) {
        unsigned mid = low + (high - low) / 2;
        if (g_oid[mid].address == address) return g_oid[mid].id;
        if (g_oid[mid].address < address) low = mid + 1;
        else high = mid;
    }
    return 0;
}

uint32_t save_block(uint32_t address, uint32_t size) {
    for (unsigned i = g_block_first; i < g_srec_count; ++i) {
        if (g_srec[i].address == address) return g_srec[i].id;
    }
    if (g_srec_count >= kSaveRecords || g_save_block_bytes + size > kSaveBlockBytes) {
        if (!g_save_overflow) {
            save_row("# save overflow blocks=%u bytes=%llu\n", g_srec_count - g_block_first,
                     (unsigned long long)g_save_block_bytes);
        }
        ++g_save_overflow;
        return 0;
    }
    int index = save_rec(save::kBlocks, kKindBlock, 0xFFFFFFFFu, address, size);
    if (index < 0) {
        ++g_save_overflow;
        return 0;
    }
    g_save_block_bytes += size;
    return g_srec[index].id;
}

void save_excluded(uint32_t address, uint32_t size, unsigned cls) {
    for (unsigned i = 0; i < g_save_excl_count; ++i) {
        if (g_save_excl_addr[i] == address) return;
    }
    if (g_save_excl_count < 8192) g_save_excl_addr[g_save_excl_count++] = address;
    if (cls == 3) {
        ++g_save_excl_big;
    } else if (cls == 2) {
        ++g_save_excl_capped;
        g_save_excl_capped_bytes += size;
    } else if (cls == 1) {
        ++g_save_excl_tornado;
        g_save_excl_tornado_bytes += size;
    } else {
        ++g_save_excl_other;
        g_save_excl_other_bytes += size;
    }
}

void save_classify(SaveRec& rec) {
    for (uint32_t off = 0; off + 4 <= rec.size; off += 4) {
        uint32_t value = 0;
        std::memcpy(&value, rec.body + off, 4);
        if (!value) {
            ++g_save_unres;
            continue;
        }
        const bool named_field = rec.address == g_save_tornado && off == kRingHeadOff;
        uint32_t id = save_object(value);
        if (!id && value >= 0x10000 && value < 0x7FFF0000 && private_rw(value)) {
            SIZE_T size = heap_block(value);
            if (named_field && size && size <= 0x100000) id = save_block(value, (uint32_t)size);
            if (!id) {

                if (size != (SIZE_T)-1 && size) {

                    unsigned cls = size > 0x100000 ? 3
                                                   : (named_field ? 2
                                                                  : (rec.address == g_save_tornado
                                                                         ? 1
                                                                         : 0));
                    save_excluded(value, (uint32_t)size, cls);
                }
                if (!save_patch(rec, off, 0)) return;
                if (!g_save_raw) std::memset(rec.body + off, 0, 4);
                ++g_save_nameonly;
                continue;
            }
        }
        if (id) {
            if (!save_patch(rec, off, id)) return;
            if (!g_save_raw) std::memset(rec.body + off, 0, 4);
            continue;
        }
        ++g_save_unres;
    }
}

unsigned child_walk(uint32_t spawner, uint32_t* out) {
    uint32_t node = read_dword_if(spawner + kChildHeadOff);
    unsigned n = 0;
    while (node && n < kSaveChildren) {
        bool seen = false;
        for (unsigned i = 0; i < n && !seen; ++i) seen = out[i] == node;
        if (seen) break;
        out[n++] = node;
        node = read_dword_if(node + kChildNext);
    }
    return n;
}

void save_write(unsigned frame) {
    if (!g_save_path[0]) {
        save_row("# save refused (no TJ_SAVE_PATH)\n");
        return;
    }
    if (under_game(g_save_path)) {
        save_row("# save refused (path under game)\n");
        return;
    }
    census_classes();
    if (!g_class_count) {
        save_row("# save refused (no class table)\n");
        return;
    }
    uint32_t root[3] = {};
    for (unsigned r = 0; r < 3; ++r) {
        root[r] = read_dword_if(kRootGlobal[r]);
        if (!root[r]) {
            save_row("# save refused (root %s is zero)\n", kRootName[r]);
            return;
        }
    }
    uint32_t array = read_dword_if(root[1] + kArrayOff);
    uint32_t high = read_dword_if(root[1] + kHighWaterOff);
    uint32_t free_base = read_dword_if(root[1] + kFreeStackOff);
    uint32_t free_count = read_dword_if(root[1] + kFreeCountOff);
    if (!array || free_count > kSlots || (free_count && !free_base)) {
        save_row("# save refused (array or free stack unreadable)\n");
        return;
    }
    g_srec = static_cast<SaveRec*>(std::calloc(kSaveRecords, sizeof(SaveRec)));
    uint32_t* slot_id = static_cast<uint32_t*>(std::calloc(kSlots, sizeof(uint32_t)));
    if (!g_srec || !slot_id) {
        std::free(slot_id);
        save_clear();
        save_row("# save refused (no memory)\n");
        return;
    }

    for (unsigned r = 0; r < 3; ++r) {
        if (save_rec(save::kRoots, kKindRoot, 0xFFFFFFFFu, root[r], kRootSize[r]) < 0) {
            std::free(slot_id);
            save_clear();
            save_row("# save refused (root %s unreadable)\n", kRootName[r]);
            return;
        }
    }

    for (unsigned i = 0; i < kSlots; ++i) {
        uint32_t object = read_dword_if(array + 4 * i);
        if (!object) continue;
        const census::Class* found = class_of(object);
        if (!found) {
            ++g_save_unknown;
            continue;
        }
        int index = save_rec(save::kActors, kKindActor, found->id, object, found->size);
        if (index >= 0) slot_id[i] = g_srec[index].id;
    }
    if (g_save_unknown) save_row("# save unknown vtable count=%u\n", g_save_unknown);

    find_spawners();
    uint32_t child[kSaveChildren] = {};
    for (unsigned i = 0; i < g_spawn_count; ++i) {
        save_rec(save::kSpawners, kKindSpawner, 0xFFFFFFFFu, g_spawn[i], kSpawnSize);
        save_rec(save::kSpawners, kKindOwner, 0xFFFFFFFFu, g_spawn_owner[i], kSpawnOwnerSize);
        unsigned walked = child_walk(g_spawn[i], child);
        for (unsigned c = 0; c < walked; ++c) {
            save_rec(save::kSpawners, kKindChild, 0xFFFFFFFFu, child[c], kSpawnChildSize);
        }
        save_row("# save children spawner=%u walked=%u count=%u\n", i, walked,
                 read_dword_if(g_spawn[i] + kChildCountOff));
    }

    g_block_first = g_srec_count;
    uint32_t ring = read_dword_if(root[0] + 0x1E74);
    uint32_t tornado = ring < kSlots ? read_dword_if(array + 4 * ring) : 0;
    g_save_tornado = tornado;
    uint32_t head = tornado ? read_dword_if(tornado + kRingHeadOff) : 0;
    uint32_t nodes = tornado ? read_dword_if(tornado + kRingCountOff) : 0;
    if (head && nodes) save_rec(save::kBlocks, kKindBlock, 0xFFFFFFFFu, head, kRingNodeBytes);
    g_oid_count = g_block_first;
    g_oid = static_cast<ObjectId*>(std::calloc(g_oid_count ? g_oid_count : 1, sizeof(ObjectId)));
    if (!g_oid) {
        std::free(slot_id);
        save_clear();
        save_row("# save refused (no memory)\n");
        return;
    }
    for (unsigned i = 0; i < g_oid_count; ++i) {
        g_oid[i].address = g_srec[i].address;
        g_oid[i].id = g_srec[i].id;
    }
    std::qsort(g_oid, g_oid_count, sizeof(ObjectId), object_id_cmp);

    scan_regions();
    for (unsigned i = 0; i < g_srec_count; ++i) {
        if (g_srec[i].kind != kKindBlock) save_classify(g_srec[i]);
    }
    save_row("# save block rule tornado=%08X kept=%u bytes=%llu\n", g_save_tornado,
             g_srec_count - g_block_first, (unsigned long long)g_save_block_bytes);
    save_row("# save excluded other-object blocks=%u bytes=%llu reason=not-a-tornado-field\n",
             g_save_excl_other, (unsigned long long)g_save_excl_other_bytes);
    save_row("# save excluded tornado-heap blocks=%u bytes=%llu "
             "reason=census-9.5-not-simulation-state\n",
             g_save_excl_tornado, (unsigned long long)g_save_excl_tornado_bytes);
    save_row("# save excluded capped blocks=%u bytes=%llu reason=ring-field-over-a-cap\n",
             g_save_excl_capped, (unsigned long long)g_save_excl_capped_bytes);
    save_row("# save excluded over-ceiling blocks=%u reason=size-over-1MB-not-a-block-here\n",
             g_save_excl_big);

    uint32_t bytes[save::kSectionCount] = {};
    bytes[save::kGlobals - 1] = 2 * 7 * 4;
    bytes[save::kClassMap - 1] = 12 * g_class_count;
    bytes[save::kArray - 1] = 12 + 4 * free_count + 4 * kSlots;
    bytes[save::kRand - 1] = 4;
    uint64_t total = save::kHeaderBytes + save::kSectionCount * save::kSectionEntryBytes;
    unsigned patch_total = 0;
    for (unsigned i = 0; i < g_srec_count; ++i) {
        const SaveRec& rec = g_srec[i];
        bytes[rec.tag - 1] += 20 + 12 * rec.patch_count + rec.size;
        patch_total += rec.patch_count;
    }
    for (unsigned i = 0; i < save::kSectionCount; ++i) total += bytes[i];
    unsigned char* image = static_cast<unsigned char*>(std::malloc((size_t)total));
    if (!image) {
        std::free(slot_id);
        std::free(g_oid);
        g_oid = nullptr;
        g_oid_count = 0;
        save_clear();
        save_row("# save refused (no memory)\n");
        return;
    }
    save_row("# save refs unresolvable=%u nameonly=%u unknown=%u overflow=%u blockbytes=%llu\n",
             g_save_unres, g_save_nameonly, g_save_unknown, g_save_overflow,
             (unsigned long long)g_save_block_bytes);
    save::Section dir[save::kSectionCount] = {};
    uint32_t offset = save::kHeaderBytes + save::kSectionCount * save::kSectionEntryBytes;
    for (unsigned i = 0; i < save::kSectionCount; ++i) {
        dir[i].tag = i + 1;
        dir[i].offset = offset;
        dir[i].bytes = bytes[i];
        offset += bytes[i];
    }
    save::Header header = {};
    std::memcpy(header.magic, save::kMagic, sizeof header.magic);
    header.version = save::kVersion;
    header.header_bytes = save::kHeaderBytes;
    header.section_count = save::kSectionCount;
    header.record_count = g_srec_count;
    header.class_count = g_class_count;
    header.flags = g_faithful ? 0 : 1;
    image_id(header.image_sha256);
    save::write_header(image, header);
    save::write_directory(image + save::kHeaderBytes, dir);
    unsigned char* cursor[save::kSectionCount] = {};
    for (unsigned i = 0; i < save::kSectionCount; ++i) cursor[i] = image + dir[i].offset;
    for (unsigned i = 0; i < 7; ++i) save::put32(cursor[0], kGlobals[i]);
    for (unsigned i = 0; i < 7; ++i) save::put32(cursor[0], read_dword_if(kGlobals[i]));
    for (unsigned i = 0; i < g_class_count; ++i) {
        save::put32(cursor[1], g_class[i].id);
        save::put32(cursor[1], g_class[i].vtable);
        save::put32(cursor[1], g_class[i].size);
    }
    save::put32(cursor[3], kSlots);
    save::put32(cursor[3], high);
    save::put32(cursor[3], free_count);
    for (unsigned i = 0; i < free_count; ++i) {
        save::put32(cursor[3], read_dword_if(free_base + 4 * i));
    }
    for (unsigned i = 0; i < kSlots; ++i) save::put32(cursor[3], slot_id[i]);
    for (unsigned i = 0; i < g_srec_count; ++i) {
        const SaveRec& rec = g_srec[i];
        unsigned char* p = cursor[rec.tag - 1];
        save::put32(p, rec.id);
        save::put32(p, rec.kind);
        save::put32(p, rec.class_id);
        save::put32(p, rec.size);
        save::put32(p, rec.patch_count);
        for (unsigned k = 0; k < rec.patch_count; ++k) {
            save::put32(p, rec.patch[k].offset);
            save::put32(p, rec.patch[k].block);
            save::put32(p, rec.patch[k].at);
        }
        std::memcpy(p, rec.body, rec.size);
        cursor[rec.tag - 1] = p + rec.size;
    }
    save::put32(cursor[7], read_dword_if(kRandGlobal));
    HANDLE file = CreateFileA(g_save_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    bool ok = file != INVALID_HANDLE_VALUE &&
              WriteFile(file, image, (DWORD)total, &written, nullptr) == TRUE && written == total;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (ok) {
        save_row("# save write frame=%u records=%u blocks=%u patch=%u free=%u bytes=%u "
                 "path=%s\n",
                 frame, g_srec_count, g_srec_count - g_block_first, patch_total, free_count,
                 (unsigned)total, g_save_path);
    } else {
        save_row("# save refused (write failed path=%s)\n", g_save_path);
    }
    std::free(image);
    std::free(slot_id);
    std::free(g_oid);
    g_oid = nullptr;
    g_oid_count = 0;
    save_clear();
}

struct FileRec {
    uint32_t id;
    uint32_t kind;
    uint32_t class_id;
    uint32_t size;
    uint32_t vtable;
    uint32_t patch_count;
    bool paired;
    save::Patch* patch;
    const unsigned char* body;
};

struct LoadState {
    unsigned char* data;
    FileRec* rec;
    unsigned count;
    save::RefTable* ref;
    unsigned* by_id;
    unsigned* slot_of;
    uint32_t max_id;
    unsigned paired;
    unsigned mismatched;
    unsigned allocated;
    unsigned blocks;
};

void load_free(LoadState* s) {
    for (unsigned i = 0; i < s->count; ++i) std::free(s->rec[i].patch);
    std::free(s->rec);
    std::free(s->ref);
    std::free(s->by_id);
    std::free(s->slot_of);
    std::free(s->data);
    std::memset(s, 0, sizeof *s);
}

bool load_stop(unsigned stage, LoadState* s) {
    if (!g_load_stop || g_load_stop > stage) return false;
    save_row("# load stopped stage=%u\n", g_load_stop);
    load_free(s);
    return true;
}

uint32_t load_u32(const unsigned char* p) {
    uint32_t value = 0;
    std::memcpy(&value, p, 4);
    return value;
}

uint32_t class_vtable(const unsigned char* map, unsigned count, uint32_t id, uint32_t size) {
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char* p = map + 12 * i;
        if (load_u32(p) == id && load_u32(p + 8) == size) return load_u32(p + 4);
    }
    return 0;
}

const KeepLocal* keep_local(const FileRec& rec) {
    const uint32_t key = rec.kind == kKindActor
                             ? rec.class_id
                             : (rec.body && rec.size >= 4 ? load_u32(rec.body) : 0);
    for (unsigned i = 0; i < sizeof KEEP_LOCAL / sizeof KEEP_LOCAL[0]; ++i) {
        const KeepLocal& entry = KEEP_LOCAL[i];
        if (entry.kind == rec.kind && entry.size == rec.size && entry.key == key) return &entry;
    }
    return nullptr;
}

bool load_keep(FileRec& rec, unsigned* kept) {
    const KeepLocal* entry = keep_local(rec);
    if (!entry) return true;
    const unsigned count = rec.patch_count + entry->count;
    save::Patch* grown =
        static_cast<save::Patch*>(std::realloc(rec.patch, (size_t)count * sizeof(save::Patch)));
    if (!grown) return false;
    rec.patch = grown;
    for (unsigned i = 0; i < entry->count; ++i) {
        save::Patch& patch = rec.patch[rec.patch_count + i];
        patch.offset = KEEP_LOCAL_OFFSET[entry->first + i];
        patch.block = 0;
        patch.at = 0;
        if (patch.offset > rec.size || rec.size - patch.offset < 4) return false;
        ++*kept;
    }
    rec.patch_count = count;
    return true;
}

bool load_records(const unsigned char* section, unsigned bytes, FileRec* out, unsigned capacity,
                  unsigned* count, uint32_t* max_id) {
    const unsigned char* p = section;
    const unsigned char* end = section + bytes;
    while (p < end) {
        if ((unsigned)(end - p) < 20 || *count == capacity) return false;
        FileRec& rec = out[*count];
        std::memset(&rec, 0, sizeof rec);
        rec.id = load_u32(p);
        rec.kind = load_u32(p + 4);
        rec.class_id = load_u32(p + 8);
        rec.size = load_u32(p + 12);
        rec.patch_count = load_u32(p + 16);
        p += 20;
        if (!rec.id || rec.patch_count > (unsigned)(end - p) / 12) return false;
        if (rec.patch_count) {
            rec.patch = static_cast<save::Patch*>(std::malloc((size_t)rec.patch_count * 12));
            if (!rec.patch) return false;
            std::memcpy(rec.patch, p, (size_t)rec.patch_count * 12);
        }
        p += 12 * rec.patch_count;
        if (rec.size > (unsigned)(end - p)) return false;
        rec.body = p;
        p += rec.size;
        if (rec.id > *max_id) *max_id = rec.id;
        ++*count;
    }
    return true;
}

bool load_body(const FileRec& rec, uint32_t at, const save::RefTable& refs) {
    for (unsigned k = 0; k < rec.patch_count; ++k) {
        if (rec.patch[k].block == 0) continue;
        uint32_t ignore = 0;
        if (rec.patch[k].offset > rec.size || rec.size - rec.patch[k].offset < 4 ||
            !refs.find(rec.patch[k].block, &ignore)) {
            save_row("# load resolve failed record=%u block=%u\n", rec.id, rec.patch[k].block);
            return false;
        }
    }
    unsigned char* body = static_cast<unsigned char*>(std::malloc(rec.size ? rec.size : 1));
    if (!body) {
        save_row("# load resolve failed record=%u block=0\n", rec.id);
        return false;
    }
    std::memcpy(body, rec.body, rec.size);
    bool ok = save::resolve_body(refs, body, rec.size, rec.patch, rec.patch_count);
    for (unsigned k = 0; ok && k < rec.patch_count; ++k) {
        if (!rec.patch[k].block) {
            std::memcpy(body + rec.patch[k].offset, &rec.patch[k].at, 4);
        }
    }
    unsigned copy = rec.size;
    if (g_load_bytes && g_load_bytes < copy) copy = g_load_bytes;
    if (ok) std::memcpy(reinterpret_cast<void*>((uintptr_t)at), body, copy);
    std::free(body);
    return ok;
}

void load_chain(const LoadState& s, uint32_t spawner, unsigned first, unsigned end) {
    uint32_t head = 0;
    uint32_t previous = 0;
    unsigned linked = 0;
    for (unsigned i = first; i < end; ++i) {
        uint32_t at = 0;
        if (!s.ref->find(s.rec[i].id, &at)) continue;
        if (previous) {
            write_dword(previous + kChildNext, at);
            write_dword(at + kChildPrev, previous);
        } else {
            head = at;
        }
        previous = at;
        ++linked;
    }
    if (previous) write_dword(previous + kChildNext, 0);
    if (head) write_dword(head + kChildPrev, 0);
    write_dword(spawner + kChildHeadOff, head);
    write_dword(spawner + kChildCountOff, linked);
}

void load_apply(unsigned frame) {
    LoadState s = {};
    FILE* file = std::fopen(g_load_path, "rb");
    if (!file) {
        save_row("# load refused (no file %s)\n", g_load_path);
        return;
    }
    _fseeki64(file, 0, SEEK_END);
    long long bytes = _ftelli64(file);
    _fseeki64(file, 0, SEEK_SET);
    const long long least = save::kHeaderBytes + save::kSectionCount * save::kSectionEntryBytes;
    if (bytes < least || bytes > (long long)(256u << 20)) {
        std::fclose(file);
        save_row("# load refused (size %lld)\n", bytes);
        return;
    }
    s.data = static_cast<unsigned char*>(std::malloc((size_t)bytes));
    if (!s.data || std::fread(s.data, 1, (size_t)bytes, file) != (size_t)bytes) {
        std::fclose(file);
        load_free(&s);
        save_row("# load refused (read)\n");
        return;
    }
    std::fclose(file);
    const unsigned size = (unsigned)bytes;
    save::Header header = {};
    if (!save::read_header(s.data, size, &header) ||
        header.section_count != save::kSectionCount) {
        load_free(&s);
        save_row("# load refused (header)\n");
        return;
    }
    save::Section dir[save::kSectionCount] = {};
    for (unsigned i = 0; i < save::kSectionCount; ++i) {
        const unsigned char* p = s.data + save::kHeaderBytes + i * save::kSectionEntryBytes;
        dir[i].tag = load_u32(p);
        dir[i].offset = load_u32(p + 4);
        dir[i].bytes = load_u32(p + 8);
        if (dir[i].tag != i + 1 || !save::section_valid(dir[i], size)) {
            load_free(&s);
            save_row("# load refused (section %u)\n", i + 1);
            return;
        }
    }
    unsigned char want[32] = {};
    image_id(want);
    if (std::memcmp(want, header.image_sha256, 16) != 0) {
        load_free(&s);
        save_row("# load refused (image)\n");
        return;
    }
    save_row("# load stage 1 file\n");
    if (load_stop(1, &s)) return;
    const save::Section& map_section = dir[save::kClassMap - 1];
    if (header.class_count > 4096 || map_section.bytes != 12 * header.class_count) {
        load_free(&s);
        save_row("# load refused (class map)\n");
        return;
    }
    save_row("# load stage 2 map classes=%u\n", header.class_count);
    if (load_stop(2, &s)) return;
    const unsigned char* map = s.data + map_section.offset;
    s.rec = static_cast<FileRec*>(std::calloc(kSaveRecords, sizeof(FileRec)));
    if (!s.rec) {
        load_free(&s);
        save_row("# load refused (no memory)\n");
        return;
    }
    const unsigned tags[4] = {save::kRoots, save::kActors, save::kBlocks, save::kSpawners};
    bool ok = true;
    for (unsigned t = 0; t < 4 && ok; ++t) {
        const save::Section& section = dir[tags[t] - 1];
        ok = load_records(s.data + section.offset, section.bytes, s.rec, kSaveRecords, &s.count,
                          &s.max_id);
    }
    if (!ok) {
        load_free(&s);
        save_row("# load refused (records)\n");
        return;
    }
    save_row("# load stage 3 records=%u\n", s.count);
    if (load_stop(3, &s)) return;

    for (unsigned i = 0; i < s.count; ++i) {
        if (s.rec[i].kind != kKindActor) continue;
        s.rec[i].vtable = class_vtable(map, header.class_count, s.rec[i].class_id, s.rec[i].size);
        if (!s.rec[i].vtable) {
            const uint32_t class_id = s.rec[i].class_id;
            const uint32_t record_id = s.rec[i].id;
            load_free(&s);
            save_row("# load refused (class %u record %u)\n", class_id, record_id);
            return;
        }
    }
    unsigned kept = 0;
    for (unsigned i = 0; i < s.count; ++i) {
        if (!load_keep(s.rec[i], &kept)) {
            load_free(&s);
            save_row("# load refused (keep-local)\n");
            return;
        }
    }
    save_row("# load keep-local classes=%u offsets=%u patches=%u\n",
             (unsigned)(sizeof KEEP_LOCAL / sizeof KEEP_LOCAL[0]),
             (unsigned)(sizeof KEEP_LOCAL_OFFSET / sizeof KEEP_LOCAL_OFFSET[0]), kept);
    s.by_id = static_cast<unsigned*>(std::calloc((size_t)s.max_id + 1, sizeof(unsigned)));
    s.slot_of = static_cast<unsigned*>(std::calloc(s.count ? s.count : 1, sizeof(unsigned)));
    s.ref = static_cast<save::RefTable*>(std::calloc(1, sizeof(save::RefTable)));
    if (!s.by_id || !s.slot_of || !s.ref) {
        load_free(&s);
        save_row("# load refused (no memory)\n");
        return;
    }
    s.ref->clear();
    for (unsigned i = 0; i < s.count; ++i) s.by_id[s.rec[i].id] = i + 1;
    save_row("# load stage 4 tables\n");
    if (load_stop(4, &s)) return;

    const save::Section& arr = dir[save::kArray - 1];
    if (arr.bytes < 12) {
        load_free(&s);
        save_row("# load refused (array)\n");
        return;
    }
    const unsigned char* arr_p = s.data + arr.offset;
    const uint32_t slots = load_u32(arr_p);
    const uint32_t high_file = load_u32(arr_p + 4);
    const uint32_t free_count = load_u32(arr_p + 8);
    if (slots != kSlots || free_count > slots ||
        arr.bytes != 12 + 4 * free_count + 4 * slots) {
        load_free(&s);
        save_row("# load refused (array)\n");
        return;
    }
    const uint32_t* free_id = reinterpret_cast<const uint32_t*>(arr_p + 12);
    const uint32_t* slot_id = reinterpret_cast<const uint32_t*>(arr_p + 12 + 4 * free_count);
    for (unsigned i = 0; i < slots; ++i) {
        uint32_t id = slot_id[i];
        if (!id || id > s.max_id) continue;
        if (s.by_id[id]) s.slot_of[s.by_id[id] - 1] = i + 1;
    }

    uint32_t scene_now = read_dword_if(kSceneObject);
    uint32_t high_now = scene_now ? read_dword_if(scene_now + kHighWaterOff) : 0;
    find_spawners();
    bool refused = false;
    uint32_t root_paired[3] = {};
    unsigned root_seen = 0;
    unsigned spawn_seen = 0;
    unsigned child_seen = 0;
    unsigned child_count = 0;
    uint32_t child[kSaveChildren] = {};
    for (unsigned i = 0; i < s.count && !refused; ++i) {
        FileRec& rec = s.rec[i];
        uint32_t object = 0;
        if (rec.kind == kKindRoot) {
            if (root_seen < 3) {
                object = read_dword_if(kRootGlobal[root_seen]);
                root_paired[root_seen] = object;
            }
            ++root_seen;
        } else if (rec.kind == kKindActor) {
            unsigned slot_plus = s.slot_of[i];
            if (slot_plus && root_paired[1]) {
                uint32_t array = read_dword_if(root_paired[1] + kArrayOff);
                if (array) object = read_dword_if(array + 4 * (slot_plus - 1));
            }
        } else if (rec.kind == kKindSpawner) {
            if (spawn_seen < g_spawn_count) object = g_spawn[spawn_seen];
            child_seen = 0;
            child_count = spawn_seen < g_spawn_count ? child_walk(g_spawn[spawn_seen], child) : 0;
            ++spawn_seen;
        } else if (rec.kind == kKindOwner) {
            if (spawn_seen && spawn_seen - 1 < g_spawn_count) object = g_spawn_owner[spawn_seen - 1];
        } else if (rec.kind == kKindChild) {
            if (child_seen < child_count) object = child[child_seen];
            ++child_seen;
        } else {
            continue;
        }
        uint32_t first = object ? read_dword_if(object) : 0;
        bool match = false;
        if (object) {
            if (rec.kind == kKindActor) {
                match = first == rec.vtable;
            } else {
                match = rec.size >= 4 && first == load_u32(rec.body);
            }
        }
        if (!match) {
            ++s.mismatched;
            continue;
        }
        ++s.paired;
        rec.paired = true;
        if (s.ref->count == save::RefTable::kMaxBlocks) {
            save_row("# load refused (ref table full)\n");
            refused = true;
            break;
        }
        s.ref->add(rec.id, object);
        for (unsigned k = 0; k < rec.patch_count; ++k) {
            uint32_t value = 0;
            if (rec.patch[k].offset > rec.size || rec.size - rec.patch[k].offset < 4) continue;
            if (!probe_read_dword(object + rec.patch[k].offset, &value)) continue;
            if (rec.patch[k].block == 0) {

                rec.patch[k].at = value;
                continue;
            }
            if (!value) continue;
            if (s.ref->count == save::RefTable::kMaxBlocks) {
                save_row("# load refused (ref table full)\n");
                refused = true;
                break;
            }
            s.ref->add(rec.patch[k].block, value);
        }
    }

    if (!refused && !root_paired[1]) {
        save_row("# load refused (no scene)\n");
        refused = true;
    }
    if (!refused && !s.paired) {
        save_row("# load refused (nothing paired)\n");
        refused = true;
    }
    for (unsigned i = 0; i < s.count && !refused; ++i) {
        const FileRec& rec = s.rec[i];
        for (unsigned k = 0; k < rec.patch_count; ++k) {
            uint32_t id = rec.patch[k].block;
            if (!id) continue;
            uint32_t at = 0;
            if (s.ref->find(id, &at)) continue;
            unsigned index = id <= s.max_id ? s.by_id[id] : 0;
            if (!index) {
                save_row("# load refused (no record %u)\n", id);
                refused = true;
                break;
            }
            if (s.ref->count == save::RefTable::kMaxBlocks) {
                save_row("# load refused (ref table full)\n");
                refused = true;
                break;
            }
            uint32_t made = alloc_object(s.rec[index - 1].size);
            if (!made) {
                save_row("# load refused (alloc %u failed)\n", id);
                refused = true;
                break;
            }
            save_row("# load allocated id=%u size=%u at=%08X\n", id, s.rec[index - 1].size, made);
            s.ref->add(id, made);
            ++s.allocated;
        }
    }
    save_row("# load stage 5 paired=%u mismatched=%u allocated=%u\n", s.paired, s.mismatched,
             s.allocated);
    if (!refused && load_stop(5, &s)) return;

    if (!refused) {
        unsigned roots_seen = 0;
        unsigned actors_written = 0;
        for (unsigned i = 0; i < s.count && !refused; ++i) {
            const FileRec& rec = s.rec[i];
            if (rec.kind == kKindBlock || !rec.paired) continue;
            bool write = true;
            if (rec.kind == kKindRoot) {
                write = (g_load_root & (1u << roots_seen)) != 0;
                ++roots_seen;
            } else if (rec.kind == kKindActor) {
                write = (g_load_mask & 2) != 0;

                if (write && g_load_actors && actors_written >= g_load_actors) write = false;
                if (write) {
                    ++actors_written;
                    if (g_load_actors) {
                        save_row("# load actor n=%u id=%u class=%u size=%u\n", actors_written,
                                 rec.id, rec.class_id, rec.size);
                    }
                }
            } else {
                write = (g_load_mask & 4) != 0;
            }
            if (!write) continue;
            uint32_t at = 0;
            if (!s.ref->find(rec.id, &at)) continue;
            if (!committed(at, rec.size, true)) {
                ++s.mismatched;
                continue;
            }
            if (!load_body(rec, at, *s.ref)) refused = true;
        }
        save_row("# load bodies actors=%u cap=%u\n", actors_written, g_load_actors);
        save_row("# load stage 6 bodies\n");
        if (!refused && load_stop(6, &s)) return;
        for (unsigned i = 0; i < s.count && !refused; ++i) {
            const FileRec& rec = s.rec[i];
            if (rec.kind != kKindBlock) continue;
            uint32_t at = 0;
            if (!s.ref->find(rec.id, &at)) continue;
            if (!committed(at, rec.size, true)) continue;

            if (!(g_load_mask & 8) && !load_body(rec, at, *s.ref)) {
                refused = true;
                break;
            }
            ++s.blocks;
        }
        save_row("# load stage 7 blocks=%u%s\n", s.blocks,
                 (g_load_mask & 8) ? " bodies=kept" : "");
        if (!refused && load_stop(7, &s)) return;
    }
    if (!refused) {
        uint32_t scene = root_paired[1];
        uint32_t array = scene ? read_dword_if(scene + kArrayOff) : 0;
        save_row("# load array scene=%08X array=%08X high=%u free=%u\n", scene, array, high_file,
                 free_count);
        for (unsigned i = 0; array && i < slots; ++i) {
            uint32_t value = 0;
            uint32_t id = slot_id[i];
            if (id && id <= s.max_id && s.by_id[id] && s.rec[s.by_id[id] - 1].paired) {
                s.ref->find(id, &value);
            }
            write_dword(array + 4 * i, value);
        }
        write_dword(scene + kHighWaterOff, high_file);
        uint32_t free_base = scene ? read_dword_if(scene + kFreeStackOff) : 0;
        if (free_base) {
            for (unsigned i = 0; i < free_count; ++i) {
                write_dword(free_base + 4 * i, free_id[i]);
            }
        }
        write_dword(scene + kFreeCountOff, free_count);
        for (unsigned i = 0; i < s.count; ++i) {
            if (s.rec[i].kind != kKindSpawner) continue;
            uint32_t at = 0;
            if (!s.ref->find(s.rec[i].id, &at)) continue;
            unsigned first = i + 1;
            while (first < s.count && s.rec[first].kind != kKindChild &&
                   s.rec[first].kind != kKindSpawner) {
                ++first;
            }
            unsigned end = first;
            while (end < s.count && s.rec[end].kind == kKindChild) ++end;
            load_chain(s, at, first, end);
        }
        save_row("# load stage 8 array\n");
        if (load_stop(8, &s)) return;
        const save::Section& rand_section = dir[save::kRand - 1];
        uint32_t rand_value = load_u32(s.data + rand_section.offset);
        write_dword(kRandGlobal, rand_value);
        save_row("# load stage 9 rand=%08X\n", rand_value);
        save_row("# load applied frame=%u high=%u spawners=%u records=%u paired=%u "
                 "mismatched=%u blocks=%u allocated=%u free=%u rand=%08X\n",
                 frame, high_now, g_spawn_count, s.count, s.paired, s.mismatched, s.blocks,
                 s.allocated, free_count, rand_value);
    }
    load_free(&s);
}

void arm_save() {
    char text[32] = {};
    SetLastError(ERROR_SUCCESS);
    DWORD n = GetEnvironmentVariableA("TJ_SAVE_AT", text, sizeof text);
    if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return;
    if (n >= sizeof text || !std::strtoul(text, nullptr, 10)) {
        save_row("# save refused (frame number needed)\n");
        return;
    }
    g_save_at = (unsigned)std::strtoul(text, nullptr, 10);
    DWORD m = GetEnvironmentVariableA("TJ_SAVE_PATH", g_save_path, sizeof g_save_path);
    if (m == 0 || m >= sizeof g_save_path) g_save_path[0] = 0;
    g_save = true;
    save_row("# save armed at=%u path=%s\n", g_save_at, g_save_path[0] ? g_save_path : "-");
    char raw[8] = {};
    if (GetEnvironmentVariableA("TJ_SAVE_RAW", raw, sizeof raw) > 0) {
        g_save_raw = std::strtoul(raw, nullptr, 10) != 0;
        save_row("# save raw=%u\n", g_save_raw ? 1u : 0u);
    }
}

void arm_load() {
    SetLastError(ERROR_SUCCESS);
    DWORD n = GetEnvironmentVariableA("TJ_LOAD", g_load_path, sizeof g_load_path);
    if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return;
    if (g_faithful) {
        save_row("# load refused (faithful)\n");
        return;
    }
    if (n >= sizeof g_load_path || !g_load_path[0]) {
        save_row("# load refused (no path)\n");
        return;
    }
    char stop[16] = {};
    if (GetEnvironmentVariableA("TJ_LOAD_STOP", stop, sizeof stop) > 0) {
        g_load_stop = (unsigned)std::strtoul(stop, nullptr, 10);
    }
    char mask[16] = {};
    if (GetEnvironmentVariableA("TJ_LOAD_MASK", mask, sizeof mask) > 0) {
        g_load_mask = (unsigned)std::strtoul(mask, nullptr, 10) & 15;
    }
    char root[16] = {};
    if (GetEnvironmentVariableA("TJ_LOAD_ROOT", root, sizeof root) > 0) {
        g_load_root = (unsigned)std::strtoul(root, nullptr, 10) & 7;
    }
    char at[16] = {};
    if (GetEnvironmentVariableA("TJ_LOAD_AT", at, sizeof at) > 0) {
        g_load_at = (unsigned)std::strtoul(at, nullptr, 10);
    }
    char limit[16] = {};
    if (GetEnvironmentVariableA("TJ_LOAD_BYTES", limit, sizeof limit) > 0) {
        g_load_bytes = (unsigned)std::strtoul(limit, nullptr, 0);
    }
    char actors[16] = {};
    if (GetEnvironmentVariableA("TJ_LOAD_ACTORS", actors, sizeof actors) > 0) {
        g_load_actors = (unsigned)std::strtoul(actors, nullptr, 10);
    }
    g_load = true;
    save_row("# load armed path=%s stop=%u mask=%u root=%u at=%u actors=%u\n", g_load_path,
             g_load_stop, g_load_mask, g_load_root, g_load_at, g_load_actors);
}

void save_present() {
    if (!g_save || g_save_done || g_presents != g_save_at) return;
    g_save_done = true;
    save_write(g_presents);
}

void load_present() {
    if (!g_load || g_load_done) return;
    if (g_load_at && g_presents < g_load_at) return;
    uint32_t scene = read_dword_if(kSceneObject);
    uint32_t array = scene ? read_dword_if(scene + kArrayOff) : 0;
    uint32_t high = scene ? read_dword_if(scene + kHighWaterOff) : 0;
    if (!array || high < kLevelHigh) return;
    g_load_done = true;
    load_apply(g_presents);
}

constexpr uint32_t kImageBase = 0x400000;
constexpr uint32_t kStepDriver = 0x48B7B0;
constexpr uint32_t kStepSites[2] = {0x508F86, 0x508FDC};
constexpr unsigned kStepTrace = 600;

fixedstep::Clock g_step_clock;
bool g_step_on;
unsigned g_step_count;
unsigned g_step_trace;
LARGE_INTEGER g_step_freq;
LARGE_INTEGER g_step_last;

uint32_t image_va(uint32_t va) {
    return (uint32_t)((uintptr_t)GetModuleHandleA(nullptr) + (va - kImageBase));
}

unsigned char* step_site(uintptr_t base, uint32_t site_va) {
    unsigned char* site = (unsigned char*)(base + (site_va - kImageBase));
    if (site[0] != 0xE8) return nullptr;
    int32_t rel = 0;
    std::memcpy(&rel, site + 1, 4);
    void* target = (void*)(base + (kStepDriver - kImageBase));
    return (uintptr_t)(site + 5) + (intptr_t)rel == (uintptr_t)target ? site : nullptr;
}

void install_step_hook() {
    if (g_step_on || g_faithful || g_step_off) return;
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    unsigned char* site[2] = {step_site(base, kStepSites[0]), step_site(base, kStepSites[1])};
    bool ready = site[0] && site[1];
    if (ready) {
        DWORD old[2] = {};

        ready = VirtualProtect(site[0], 5, PAGE_EXECUTE_READWRITE, &old[0]) &&
                VirtualProtect(site[1], 5, PAGE_EXECUTE_READWRITE, &old[1]);
        if (ready) {
            for (unsigned i = 0; i < 2; ++i) {
                int32_t delta = (int32_t)((unsigned char*)&StepHook - (site[i] + 5));
                std::memcpy(site[i] + 1, &delta, 4);
                FlushInstructionCache(GetCurrentProcess(), site[i], 5);
            }
            for (unsigned i = 2; i-- > 0;) {
                DWORD restored = 0;
                VirtualProtect(site[i], 5, old[i], &restored);
            }
        }
    }
    g_step_entry = (void*)(base + (kStepDriver - kImageBase));
    g_step_delta = fixedstep::kStep;
    QueryPerformanceFrequency(&g_step_freq);
    QueryPerformanceCounter(&g_step_last);
    g_step_clock.accumulated = fixedstep::kStepSeconds;
    uint32_t sim = 0;
    uint32_t count = 0;
    if (probe_read_dword(image_va(kSimObject), &sim) && sim &&
        probe_read_dword(sim + 0x1C, &count)) {
        g_step_count = count;
    }
    g_step_on = ready;
    if (g_log) {
        std::fprintf(g_log, "# fixed step %s sites=%d seed=%u replay=%d\n",
                     g_step_on ? "on" : "OFF", g_step_on ? 2 : 0, g_step_count,
                     g_replay ? 1 : 0);
        std::fflush(g_log);
    }
}

constexpr uint32_t kArmSetter = 0x4AD210;
constexpr uint32_t kArmSites[2] = {0x42230D, 0x4AD2E7};

void* g_arm_entry;

extern "C" __declspec(naked) void ArmHook0(void) {
    __asm {
        lock inc dword ptr[g_arm_hits]
        jmp dword ptr[g_arm_entry]
    }
}

extern "C" __declspec(naked) void ArmHook1(void) {
    __asm {
        lock inc dword ptr[g_arm_hits + 4]
        jmp dword ptr[g_arm_entry]
    }
}

void install_arm_hook() {
    char text[8] = {};
    SetLastError(ERROR_SUCCESS);
    DWORD n = GetEnvironmentVariableA("TJ_ARM", text, sizeof text);
    if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return;
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    void* const stub[2] = {(void*)&ArmHook0, (void*)&ArmHook1};
    unsigned char* site[2] = {};
    bool ready = !g_faithful;
    for (unsigned i = 0; i < 2 && ready; ++i) {
        site[i] = (unsigned char*)(base + (kArmSites[i] - kImageBase));
        int32_t rel = 0;
        std::memcpy(&rel, site[i] + 1, 4);
        ready = site[i][0] == 0xE8 &&
                (uintptr_t)(site[i] + 5) + (intptr_t)rel ==
                    (uintptr_t)(base + (kArmSetter - kImageBase));
    }
    if (ready) {
        DWORD old[2] = {};
        ready = VirtualProtect(site[0], 5, PAGE_EXECUTE_READWRITE, &old[0]) &&
                VirtualProtect(site[1], 5, PAGE_EXECUTE_READWRITE, &old[1]);
        if (ready) {
            for (unsigned i = 0; i < 2; ++i) {
                int32_t delta = (int32_t)((unsigned char*)stub[i] - (site[i] + 5));
                std::memcpy(site[i] + 1, &delta, 4);
                FlushInstructionCache(GetCurrentProcess(), site[i], 5);
            }
            for (unsigned i = 2; i-- > 0;) {
                DWORD restored = 0;
                VirtualProtect(site[i], 5, old[i], &restored);
            }
        }
    }
    g_arm_entry = (void*)(base + (kArmSetter - kImageBase));
    g_arm_on = ready;
    if (g_log) {
        std::fprintf(g_log, "# arm hook %s sites=%u entry=%08X\n", ready ? "on" : "OFF",
                     ready ? 2u : 0u, (unsigned)(uintptr_t)g_arm_entry);
        std::fflush(g_log);
    }
}

void step_present() {
    if (!g_step_on || !g_log || g_presents % fixedstep::kSampleEvery) return;
    uint32_t sim = 0;
    uint32_t count = 0;
    bool ok = probe_read_dword(image_va(kSimObject), &sim) && sim &&
              probe_read_dword(sim + 0x1C, &count);
    EnterCriticalSection(&g_lock);
    if (ok) {
        std::fprintf(g_log, "# step count frame=%u proxy=%u sim=%u\n", g_presents, g_step_count,
                     count);
    } else {
        std::fprintf(g_log, "# step count frame=%u proxy=%u sim unreadable\n", g_presents,
                     g_step_count);
    }
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

bool watch_set_thread(HANDLE thread) {
    CONTEXT context = {};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread, &context)) return false;
    context.Dr0 = g_watch_address[0];
    context.Dr1 = g_watch_address[1];
    context.Dr2 = 0;
    context.Dr3 = 0;
    context.Dr6 = 0;
    context.Dr7 = watch::dr7(2);
    return SetThreadContext(thread, &context) != 0;
}

DWORD WINAPI watch_arm_thread(void*) {
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    DWORD ids[256];
    unsigned count = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry = {};
        entry.dwSize = sizeof entry;
        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID == pid && entry.th32ThreadID != self &&
                    count < 256) {
                    ids[count++] = entry.th32ThreadID;
                }
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    HANDLE handles[256];
    unsigned opened = 0;
    unsigned armed = 0;
    for (unsigned i = 0; i < count; ++i) {
        HANDLE thread =
            OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE,
                       ids[i]);
        if (!thread) continue;
        if (SuspendThread(thread) == (DWORD)-1) {
            CloseHandle(thread);
            continue;
        }
        handles[opened++] = thread;
    }
    for (unsigned i = 0; i < opened; ++i) armed += watch_set_thread(handles[i]) ? 1 : 0;
    for (unsigned i = 0; i < opened; ++i) {
        ResumeThread(handles[i]);
        CloseHandle(handles[i]);
    }
    EnterCriticalSection(&g_watch_lock);
    if (g_log) {
        std::fprintf(g_log, "# watch armed dr7=%08X threads=%u dr0=%08X dr1=%08X frame=%u\n",
                     watch::dr7(2), armed, g_watch_address[0], g_watch_address[1], g_presents);
        std::fflush(g_log);
    }
    LeaveCriticalSection(&g_watch_lock);
    return 0;
}

void watch_arm_threads() {
    HANDLE thread = CreateThread(nullptr, 0, watch_arm_thread, nullptr, 0, nullptr);
    if (!thread) return;
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
}

LONG CALLBACK watch_veh(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord ||
        info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    CONTEXT* context = info->ContextRecord;
    const DWORD hit = context->Dr6 & 0x3;
    if (!hit) {
        if (g_log && g_watch_hits < 32) {
            EnterCriticalSection(&g_watch_lock);
            std::fprintf(g_log, "# watch step frame=%u eip=%08X tid=%u dr6=%X (no B0/B1)\n",
                         g_presents, (unsigned)context->Eip, GetCurrentThreadId(),
                         (unsigned)context->Dr6);
            std::fflush(g_log);
            LeaveCriticalSection(&g_watch_lock);
            ++g_watch_hits;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    uint32_t value[2] = {g_watch_last[0], g_watch_last[1]};
    __try {
        if (hit & 1) value[0] = *(volatile uint32_t*)(uintptr_t)g_watch_address[0];
        if (hit & 2) value[1] = *(volatile uint32_t*)(uintptr_t)g_watch_address[1];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    uint32_t chain[8] = {};
    unsigned frames = 0;
    __try {
        uint32_t ebp = (uint32_t)context->Ebp;
        while (frames < 8 && ebp >= 0x10000 && ebp < 0x7FFF0000) {
            uint32_t next = *(volatile uint32_t*)(uintptr_t)ebp;
            chain[frames] = *(volatile uint32_t*)(uintptr_t)(ebp + 4);
            if (next <= ebp) break;
            ebp = next;
            ++frames;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    uint32_t stack[8] = {};
    unsigned returns = 0;
    const uint32_t esp = (uint32_t)context->Esp;
    const uint32_t low = (uint32_t)g_watch_base + 0x1000;
    const uint32_t high = (uint32_t)g_watch_base + 0x254424;
    __try {
        for (unsigned i = 0; i < 128 && returns < 8; ++i) {
            uint32_t value = *(volatile uint32_t*)(uintptr_t)(esp + 4 * i);
            if (value >= low && value < high) stack[returns++] = value;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    EnterCriticalSection(&g_watch_lock);
    if (g_log) {
        std::fprintf(g_log,
                     "# watch frame=%u eip=%08X tid=%u dr6=%X old=%08X,%08X new=%08X,%08X chain=",
                     g_presents, (unsigned)context->Eip, GetCurrentThreadId(), (unsigned)hit,
                     g_watch_last[0], g_watch_last[1], value[0], value[1]);
        for (unsigned i = 0; i < frames; ++i) std::fprintf(g_log, "%08X ", chain[i]);
        std::fprintf(g_log, "stack=");
        for (unsigned i = 0; i < returns; ++i) std::fprintf(g_log, "%08X ", stack[i]);
        std::fprintf(g_log, "\n");
        std::fflush(g_log);
    }
    g_watch_last[0] = value[0];
    g_watch_last[1] = value[1];
    ++g_watch_hits;
    LeaveCriticalSection(&g_watch_lock);
    context->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void watch_present() {
    if (!g_watch) return;
    if (!g_watch_armed) {
        uint32_t address = 0;
        if (!probe_resolve(g_watch_term, &address) || !address) return;
        g_watch_address[0] = address;
        g_watch_address[1] = address + 4;
        InitializeCriticalSection(&g_watch_lock);
        g_watch_veh = AddVectoredExceptionHandler(1, watch_veh);
        g_watch_armed = true;
        g_watch_scan_at = g_presents;
        if (g_log) {
            std::fprintf(g_log, "# watch veh=%p\n", g_watch_veh);
            std::fflush(g_log);
        }
        watch_arm_threads();
        return;
    }
    uint32_t value0 = 0;
    uint32_t value1 = 0;
    bool ok0 = probe_read_dword(g_watch_address[0], &value0);
    bool ok1 = probe_read_dword(g_watch_address[1], &value1);
    if (ok0) g_watch_last[0] = value0;
    if (ok1) g_watch_last[1] = value1;
    if (g_presents >= g_watch_scan_at + 64) {
        g_watch_scan_at = g_presents;
        watch_arm_threads();
    }
}

void log_hr(unsigned role, unsigned method, HRESULT hr) {
    if (!g_io || !g_log) return;
    bool found = false;
    EnterCriticalSection(&g_lock);
    for (unsigned i = 0; i < kHrTable; ++i) {
        HrRow& row = g_hr_rows[i];
        if (!row.used) {
            row.used = 1;
            row.role = role;
            row.method = method;
            row.hr = hr;
            row.count = 1;
            row.printed = 0;
            found = true;
            break;
        }
        if (row.role == role && row.method == method && row.hr == hr) {
            ++row.count;
            found = true;
            break;
        }
    }
    if (!found) ++g_hr_dropped;
    LeaveCriticalSection(&g_lock);
}

unsigned char* emit_io_thunk(unsigned char* q, unsigned nameid, void* real,
                             void** slot_address) {
    *q++ = 0x60;
    *q++ = 0xff; *q++ = 0x74; *q++ = 0x24; *q++ = 0x20;
    *q++ = 0x68; store32(q, nameid); q += 4;
    *q++ = 0xe8;
    unsigned char* const after = q + 4;
    q += 4;
    *q++ = 0x83; *q++ = 0xc4; *q++ = 0x08;
    *q++ = 0x61;
    *q++ = 0xc7; *q++ = 0x44; *q++ = 0x24; *q++ = 0x04;
    store32(q, (uint32_t)(uintptr_t)real);
    q += 4;
    *q++ = 0xff; *q++ = 0x25;
    store32(q, (uint32_t)(uintptr_t)slot_address);
    q += 4;
    int32_t rel = (int32_t)((intptr_t)&log_io_call - (intptr_t)after);
    std::memcpy(after - 4, &rel, 4);
    return q;
}

unsigned char* emit_io_dev_thunk(unsigned char* q, DeviceWrap* device, unsigned slot, void* real,
                                 void** slot_address) {
    *q++ = 0x60;
    *q++ = 0xff; *q++ = 0x74; *q++ = 0x24; *q++ = 0x20;
    *q++ = 0x68; store32(q, slot); q += 4;
    *q++ = 0x68; store32(q, (uint32_t)(uintptr_t)device); q += 4;
    *q++ = 0xe8;
    unsigned char* const after = q + 4;
    q += 4;
    *q++ = 0x83; *q++ = 0xc4; *q++ = 0x0c;
    *q++ = 0x61;
    *q++ = 0xc7; *q++ = 0x44; *q++ = 0x24; *q++ = 0x04;
    store32(q, (uint32_t)(uintptr_t)real);
    q += 4;
    *q++ = 0xff; *q++ = 0x25;
    store32(q, (uint32_t)(uintptr_t)slot_address);
    q += 4;
    int32_t rel = (int32_t)((intptr_t)&log_io_dev - (intptr_t)after);
    std::memcpy(after - 4, &rel, 4);
    return q;
}

bool g_trace;
unsigned g_trace_n;

void clock_read(unsigned id, void* site, int64_t value) {
    g_clock_digest = (g_clock_digest ^ (uintptr_t)site) * 0x100000001b3ull;
    g_clock_digest = (g_clock_digest ^ (uint64_t)value) * 0x100000001b3ull;
    ++g_clock_reads;
    if (!g_trace || !g_log || g_trace_n >= 800) return;
    EnterCriticalSection(&g_lock);
    std::fprintf(g_log, "# replay value %s %08X %lld tid=%u\n", io_name(id),
                 (unsigned)(uintptr_t)site, (long long)value, GetCurrentThreadId());
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
    ++g_trace_n;
}

DWORD WINAPI IoTimeGetTime() {
    log_io_call(kIoTimeGetTime, _ReturnAddress());
    if (g_replay && replay::clock_live()) {
        DWORD now = (DWORD)replay::clock_ms(0);
        clock_read(kIoTimeGetTime, _ReturnAddress(), now);
        return now;
    }
    return g_api.timeGetTime();
}

BOOL WINAPI IoQpc(LARGE_INTEGER* value) {
    log_io_call(kIoQpc, _ReturnAddress());
    if (g_replay && replay::clock_live()) {
        value->QuadPart = replay::clock_qpc();
        clock_read(kIoQpc, _ReturnAddress(), value->QuadPart);
        return TRUE;
    }
    return g_api.qpc(value);
}

BOOL WINAPI IoQpf(LARGE_INTEGER* value) {
    log_io_call(kIoQpf, _ReturnAddress());
    if (g_replay) {
        value->QuadPart = replay::qpf();
        return TRUE;
    }
    return g_api.qpf(value);
}

DWORD WINAPI IoGetTickCount() {
    log_io_call(kIoGetTickCount, _ReturnAddress());
    if (g_replay && replay::clock_live()) {
        DWORD now = (DWORD)replay::clock_ms(1);
        clock_read(kIoGetTickCount, _ReturnAddress(), now);
        return now;
    }
    return g_api.getTickCount();
}

void WINAPI IoGetLocalTime(LPSYSTEMTIME value) {
    log_io_call(kIoGetLocalTime, _ReturnAddress());
    if (g_replay) {
        int64_t tick = replay::clock_filetime_now();
        FILETIME file = {(DWORD)(uint64_t)tick, (DWORD)((uint64_t)tick >> 32)};
        FileTimeToSystemTime(&file, value);
        clock_read(kIoGetLocalTime, _ReturnAddress(), tick);
        return;
    }
    g_api.getLocalTime(value);
}

void WINAPI IoGetSystemTimeAsFileTime(LPFILETIME value) {
    log_io_call(kIoSystemTimeAsFileTime, _ReturnAddress());
    if (g_replay && replay::clock_live()) {
        int64_t ft = replay::clock_filetime();
        value->dwLowDateTime = (DWORD)(uint64_t)ft;
        value->dwHighDateTime = (DWORD)((uint64_t)ft >> 32);
        clock_read(kIoSystemTimeAsFileTime, _ReturnAddress(), ft);
        return;
    }
    g_api.systemTimeAsFileTime(value);
}

void WINAPI IoSleep(DWORD milliseconds) {
    log_io_call(kIoSleep, _ReturnAddress());
    g_api.sleep(milliseconds);
}

BOOL WINAPI IoGetCursorPos(LPPOINT value) {
    log_io_call(kIoGetCursorPos, _ReturnAddress());
    if (g_replay || g_fixed_cursor) {

        POINT origin = {replay::kCursorX, replay::kCursorY};

        long factor = !g_faithful && g_scaled && g_scale_factor ? (long)g_scale_factor : 1;
        origin.x *= factor;
        origin.y *= factor;
        if (g_hwnd) ClientToScreen(g_hwnd, &origin);
        value->x = origin.x;
        value->y = origin.y;
        return TRUE;
    }
    return g_api.getCursorPos(value);
}

BOOL WINAPI IoSetCursorPos(int x, int y) {
    log_io_call(kIoSetCursorPos, _ReturnAddress());

    if (g_replay || g_fixed_cursor) return TRUE;
    return g_api.setCursorPos(x, y);
}

void trace_log(const char* format, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_lock);
    std::fprintf(g_log, "# trace ms=%llu ", (unsigned long long)GetTickCount64());
    va_list args;
    va_start(args, format);
    std::vfprintf(g_log, format, args);
    va_end(args);
    std::fputc('\n', g_log);
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

const char* trace_message(UINT message) {
    switch (message) {
    case WM_ACTIVATE: return "WM_ACTIVATE";
    case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
    case WM_SETFOCUS: return "WM_SETFOCUS";
    case WM_KILLFOCUS: return "WM_KILLFOCUS";
    case WM_NCACTIVATE: return "WM_NCACTIVATE";
    case WM_SHOWWINDOW: return "WM_SHOWWINDOW";
    default: return nullptr;
    }
}

struct TraceWindow {
    HWND hwnd;
    WNDPROC previous;
};
TraceWindow g_trace_windows[8];

LRESULT CALLBACK trace_wndproc(HWND window, UINT message, WPARAM w, LPARAM l) {
    const char* name = trace_message(message);
    if (name) {
        HWND foreground = GetForegroundWindow();
        DWORD owner = 0;
        GetWindowThreadProcessId(foreground, &owner);
        trace_log("msg %s hwnd=%08X w=%08X l=%08X fg=%08X fgpid=%u tid=%u", name,
                  (unsigned)(uintptr_t)window, (unsigned)w, (unsigned)l,
                  (unsigned)(uintptr_t)foreground, (unsigned)owner, GetCurrentThreadId());
    }
    for (unsigned i = 0; i < sizeof g_trace_windows / sizeof g_trace_windows[0]; ++i) {
        if (g_trace_windows[i].hwnd == window) {
            return CallWindowProcA(g_trace_windows[i].previous, window, message, w, l);
        }
    }
    return DefWindowProcA(window, message, w, l);
}

DWORD WINAPI trace_foreground_thread(LPVOID) {
    HWND last = GetForegroundWindow();
    DWORD owner = 0;
    GetWindowThreadProcessId(last, &owner);
    trace_log("foreground start pid=%u hwnd=%08X", (unsigned)owner, (unsigned)(uintptr_t)last);
    for (unsigned sample = 0; sample < 1800; ++sample) {
        Sleep(100);
        HWND now = GetForegroundWindow();
        if (now == last) continue;
        last = now;
        owner = 0;
        GetWindowThreadProcessId(now, &owner);
        trace_log("foreground sample=%u pid=%u hwnd=%08X", sample, (unsigned)owner,
                  (unsigned)(uintptr_t)now);
    }
    return 0;
}

const char* site_module(uintptr_t site, uintptr_t* offset) {
    static char name[MAX_PATH];
    MEMORY_BASIC_INFORMATION region = {};
    if (VirtualQuery((void*)site, &region, sizeof region) && region.Type == MEM_IMAGE &&
        GetModuleFileNameA((HMODULE)region.AllocationBase, name, MAX_PATH)) {
        *offset = site - (uintptr_t)region.AllocationBase;
        const char* slash = std::strrchr(name, '\\');
        return slash ? slash + 1 : name;
    }
    *offset = 0;
    return "?";
}

void log_focus(const char* api, uintptr_t site, HWND window) {
    if (!g_log) return;
    uintptr_t offset = 0;
    const char* module = site_module(site, &offset);
    std::fprintf(g_log, "# background %s frame=%u site=%08X %s+%08X hwnd=%08X\n", api,
                 g_presents, (unsigned)site, module, (unsigned)offset,
                 (unsigned)(uintptr_t)window);
    std::fflush(g_log);
}

BOOL WINAPI IoSetForegroundWindow(HWND window) {
    log_io_call(kIoSetForegroundWindow, _ReturnAddress());
    if (g_background) {
        log_focus("SetForegroundWindow", (uintptr_t)_ReturnAddress(), window);
        return TRUE;
    }
    return g_api.setForegroundWindow(window);
}

BOOL WINAPI IoShowWindow(HWND window, int command) {
    log_io_call(kIoShowWindow, _ReturnAddress());

    if (g_background) { command = SW_SHOWNOACTIVATE; log_focus("ShowWindow", (uintptr_t)_ReturnAddress(), window); }
    return g_api.showWindow(window, command);
}

bool caller_is_exe(uintptr_t site) {
    MEMORY_BASIC_INFORMATION region = {};
    if (!VirtualQuery((void*)site, &region, sizeof region)) return false;
    return (HMODULE)region.AllocationBase == GetModuleHandleA(nullptr);
}

HWND WINAPI IoCreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR name, DWORD style,
                              int x, int y, int w, int h, HWND parent, HMENU menu,
                              HINSTANCE instance, LPVOID param) {
    log_io_call(kIoCreateWindowExA, _ReturnAddress());
    bool background_top = false;
    if (g_background) {
        offscreen_point(&g_background_x, &g_background_y);
        x = g_background_x;
        y = g_background_y;
        background_top = !parent;

        if (background_top) ex_style |= WS_EX_NOACTIVATE;
        if (background_top && caller_is_exe((uintptr_t)_ReturnAddress())) style &= ~WS_VISIBLE;
    }
    HWND created =
        g_api.createWindowExA(ex_style, class_name, name, style, x, y, w, h, parent, menu,
                              instance, param);

    char created_class[64] = {};
    if (created) GetClassNameA(created, created_class, sizeof created_class);
    if (background_top && g_log) {
        std::fprintf(g_log, "# background window class=%s hwnd=%08X style=%08X visible=%d\n",
                     created_class, (unsigned)(uintptr_t)created, (unsigned)style,
                     (style & WS_VISIBLE) ? 1 : 0);
        std::fflush(g_log);
    }
    if (g_activate_trace) {
        trace_log("create class=%s hwnd=%08X style=%08X ex=%08X parent=%08X visible=%d",
                  created_class, (unsigned)(uintptr_t)created, (unsigned)style,
                  (unsigned)ex_style, (unsigned)(uintptr_t)parent, (style & WS_VISIBLE) ? 1 : 0);
        if (created && !parent) {
            for (unsigned i = 0; i < sizeof g_trace_windows / sizeof g_trace_windows[0]; ++i) {
                if (g_trace_windows[i].hwnd) continue;
                g_trace_windows[i].hwnd = created;
                g_trace_windows[i].previous = (WNDPROC)(LONG_PTR)SetWindowLongA(
                    created, GWL_WNDPROC, (LONG)(intptr_t)&trace_wndproc);
                trace_log("subclass hwnd=%08X prev=%08X", (unsigned)(uintptr_t)created,
                          (unsigned)(uintptr_t)g_trace_windows[i].previous);
                break;
            }
        }
    }
    return created;
}

HWND WINAPI IoCreateWindowExW(DWORD ex_style, LPCWSTR class_name, LPCWSTR name, DWORD style,
                              int x, int y, int w, int h, HWND parent, HMENU menu,
                              HINSTANCE instance, LPVOID param) {
    log_io_call(kIoCreateWindowExW, _ReturnAddress());
    bool background_top = false;
    if (g_background) {
        offscreen_point(&g_background_x, &g_background_y);
        x = g_background_x;
        y = g_background_y;
        background_top = !parent;
        if (background_top) ex_style |= WS_EX_NOACTIVATE;
        if (background_top && caller_is_exe((uintptr_t)_ReturnAddress())) style &= ~WS_VISIBLE;
    }
    HWND created = g_api.createWindowExW(ex_style, class_name, name, style, x, y, w, h, parent,
                                         menu, instance, param);
    char created_class[64] = {};
    if (created) GetClassNameA(created, created_class, sizeof created_class);
    if (background_top && g_log) {
        std::fprintf(g_log, "# background window class=%s hwnd=%08X style=%08X visible=%d\n",
                     created_class, (unsigned)(uintptr_t)created, (unsigned)style,
                     (style & WS_VISIBLE) ? 1 : 0);
        std::fflush(g_log);
    }
    if (g_activate_trace) {
        trace_log("createw class=%s hwnd=%08X style=%08X ex=%08X parent=%08X visible=%d",
                  created_class, (unsigned)(uintptr_t)created, (unsigned)style,
                  (unsigned)ex_style, (unsigned)(uintptr_t)parent, (style & WS_VISIBLE) ? 1 : 0);
    }
    return created;
}

BOOL WINAPI IoSetWindowPos(HWND window, HWND after, int x, int y, int w, int h, UINT flags) {
    log_io_call(kIoSetWindowPos, _ReturnAddress());
    if (g_background) {
        flags |= SWP_NOACTIVATE;
        log_focus("SetWindowPos", (uintptr_t)_ReturnAddress(), window);
    }
    return g_api.setWindowPos(window, after, x, y, w, h, flags);
}

BOOL WINAPI IoBringWindowToTop(HWND window) {
    log_io_call(kIoBringWindowToTop, _ReturnAddress());
    if (g_background) {
        log_focus("BringWindowToTop", (uintptr_t)_ReturnAddress(), window);
        return TRUE;
    }
    return g_api.bringWindowToTop(window);
}

HWND WINAPI IoSetFocus(HWND window) {
    log_io_call(kIoSetFocus, _ReturnAddress());
    if (g_background) {
        log_focus("SetFocus", (uintptr_t)_ReturnAddress(), window);
        return g_api.getFocus ? g_api.getFocus() : nullptr;
    }
    return g_api.setFocus(window);
}

HWND WINAPI IoSetActiveWindow(HWND window) {
    log_io_call(kIoSetActiveWindow, _ReturnAddress());
    if (g_background) {
        log_focus("SetActiveWindow", (uintptr_t)_ReturnAddress(), window);
        return nullptr;
    }
    return g_api.setActiveWindow(window);
}

LONG CALLBACK trace_exception(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    MEMORY_BASIC_INFORMATION region = {};
    char module[MAX_PATH] = {};
    uintptr_t offset = 0;
    uintptr_t const at = (uintptr_t)info->ExceptionRecord->ExceptionAddress;
    if (VirtualQuery((void*)at, &region, sizeof region) && region.Type == MEM_IMAGE) {
        GetModuleFileNameA((HMODULE)region.AllocationBase, module, MAX_PATH);
        offset = at - (uintptr_t)region.AllocationBase;
    }
    trace_log("exception code=%08X at=%08X %s+%08X", (unsigned)info->ExceptionRecord->ExceptionCode,
              (unsigned)at, module, (unsigned)offset);

    if (info->ContextRecord) {
        const uint32_t* sp = (const uint32_t*)(uintptr_t)info->ContextRecord->Esp;
        trace_log("exception ecx=%08X edx=%08X esp=%08X tid=%u info=%08X,%08X",
                  (unsigned)info->ContextRecord->Ecx, (unsigned)info->ContextRecord->Edx,
                  (unsigned)info->ContextRecord->Esp, GetCurrentThreadId(),
                  (unsigned)info->ExceptionRecord->ExceptionInformation[0],
                  (unsigned)info->ExceptionRecord->ExceptionInformation[1]);
        const render::IndexTrace& trace = render::index_trace();
        trace_log("expand list=%08X verts=%u start=%u count=%u length=%u format=%u prim=%u "
                  "reported=%u",
                  (unsigned)(uintptr_t)trace.list, trace.vertices, trace.startIndex, trace.count,
                  trace.length, trace.format, trace.primitive, trace.reported);
        for (unsigned i = 0; i < 64 && sp; ++i) {
            MEMORY_BASIC_INFORMATION frame = {};
            char where[MAX_PATH] = {};
            uintptr_t const value = sp[i];
            if (!VirtualQuery((void*)value, &frame, sizeof frame) || frame.Type != MEM_IMAGE) continue;
            GetModuleFileNameA((HMODULE)frame.AllocationBase, where, MAX_PATH);
            trace_log("exception stack %u %08X %s+%08X", i, (unsigned)value, where,
                      (unsigned)(value - (uintptr_t)frame.AllocationBase));
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void WINAPI IoExitProcess(UINT code) {
    trace_log("exit ExitProcess code=%u site=%08X", code, (unsigned)(uintptr_t)_ReturnAddress());
    g_api.exitProcess(code);
}

BOOL WINAPI IoTerminateProcess(HANDLE process, UINT code) {
    trace_log("exit TerminateProcess code=%u site=%08X", code,
              (unsigned)(uintptr_t)_ReturnAddress());
    return g_api.terminateProcess(process, code);
}

LONG WINAPI IoSetWindowLongA(HWND window, int index, LONG value) {
    log_io_call(kIoSetWindowLongA, _ReturnAddress());
    if (g_activate_trace) {
        trace_log("call SetWindowLongA hwnd=%08X index=%d value=%08X", (unsigned)(uintptr_t)window,
                  index, (unsigned)value);
    }
    return g_api.setWindowLongA(window, index, value);
}

HRESULT WINAPI IoCoCreateInstance(const GUID* clsid, void* outer, DWORD clsctx, const GUID* iid,
                                  void** out) {
    log_io_call(kIoCoCreateInstance, _ReturnAddress());
    replay::Guid id = {};
    if (clsid) std::memcpy(&id, clsid, sizeof id);
    if ((g_replay || g_background) && replay::is_browser_clsid(&id)) {
        const HRESULT hr = (HRESULT)0x80040154L;
        if (g_log) {
            EnterCriticalSection(&g_lock);
            std::fprintf(g_log, "# io offline CoCreateInstance WebBrowser tid=%u hr=%08lX\n",
                         GetCurrentThreadId(), (unsigned long)hr);
            std::fflush(g_log);
            LeaveCriticalSection(&g_lock);
        }
        if (out) *out = nullptr;
        return hr;
    }
    return g_api.coCreateInstance(clsid, outer, clsctx, iid, out);
}

HWND WINAPI IoGetForegroundWindow() {
    log_io_call(kIoGetForegroundWindow, _ReturnAddress());
    if (g_replay && g_hwnd) return g_hwnd;
    HWND got = g_api.getForegroundWindow();

    static bool reported;
    if (g_background && g_hwnd && got == g_hwnd && !reported && g_log) {
        reported = true;
        std::fprintf(g_log, "# background foreground game frame=%u tid=%u\n", g_presents,
                     GetCurrentThreadId());
        std::fflush(g_log);
    }
    return got;
}

HWND WINAPI IoFindWindowA(LPCSTR window_class, LPCSTR title) {
    log_io_call(kIoFindWindowA, _ReturnAddress());
    HWND found = g_api.findWindowA ? g_api.findWindowA(window_class, title) : nullptr;
    if (!g_game_dir_self || !found) return found;
    DWORD owner = 0;
    GetWindowThreadProcessId(found, &owner);
    if (owner == GetCurrentProcessId()) return found;
    if (g_log) {
        std::fprintf(g_log, "# io findwindow hidden pid=%lu site=%08X\n", (unsigned long)owner,
                     (unsigned)(uintptr_t)_ReturnAddress());
        std::fflush(g_log);
    }
    return nullptr;
}

HWND WINAPI IoGetFocus() {
    log_io_call(kIoGetFocus, _ReturnAddress());
    return g_api.getFocus();
}

BOOL WINAPI IoIsIconic(HWND window) {
    log_io_call(kIoIsIconic, _ReturnAddress());
    return g_api.isIconic(window);
}

void msg_row(const char* pump, const MSG* message) {
    if (!g_log) return;
    EnterCriticalSection(&g_lock);
    std::fprintf(g_log, "# msg %s frame=%u tid=%u id=%04X hwnd=%08X w=%08X l=%08X\n", pump,
                 g_presents, GetCurrentThreadId(), (unsigned)message->message,
                 (unsigned)(uintptr_t)message->hwnd, (unsigned)message->wParam,
                 (unsigned)message->lParam);
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

BOOL WINAPI IoPeekMessage(LPMSG message, HWND window, UINT first, UINT last, UINT remove) {
    log_io_call(kIoPeekMessage, _ReturnAddress());
    for (unsigned tries = 0; tries < 64; ++tries) {
        BOOL got = g_api.peekMessage(message, window, first, last, remove);
        if (!got) return got;
        if (!g_replay && !g_fixed_cursor) return got;
        if (message->message != WM_MOUSEMOVE) {

            if (remove & PM_REMOVE) msg_row("queue", message);
            return got;
        }
        if (replay::post_take(&g_posted, (unsigned)message->lParam)) return got;
        if (!(remove & PM_REMOVE)) return got;
        g_api.peekMessage(message, window, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE);
        if (g_log) {
            std::fprintf(g_log, "# input drop mouse l=%08X frame=%u\n",
                         (unsigned)message->lParam, g_presents);
        }
    }
    return FALSE;
}

bool g_get_seen;
BOOL WINAPI IoGetMessage(LPMSG message, HWND window, UINT first, UINT last) {
    log_io_call(kIoGetMessage, _ReturnAddress());
    if (g_replay && g_log && !g_get_seen) {
        g_get_seen = true;
        EnterCriticalSection(&g_lock);
        std::fprintf(g_log, "# msg get wait frame=%u tid=%u\n", g_presents, GetCurrentThreadId());
        std::fflush(g_log);
        LeaveCriticalSection(&g_lock);
    }
    BOOL got = g_api.getMessage ? g_api.getMessage(message, window, first, last) : FALSE;
    if (g_replay && message && got > 0) msg_row("get", message);
    return got;
}

void log_open(unsigned id, const char* path, DWORD access, DWORD disposition) {
    char text[MAX_PATH + 48];
    std::snprintf(text, sizeof text, "%s access=%08X disp=%u", path ? path : "(null)", access,
                  disposition);
    log_path(id, text);
}

const char* replay_scratch(char* out, const char* path, DWORD access) {
    static char directory[MAX_PATH * 2];
    static char last_from[MAX_PATH * 2];
    if (!directory[0]) {
        char dll[MAX_PATH] = {};
        GetModuleFileNameA(g_module, dll, MAX_PATH);
        char* slash = std::strrchr(dll, '\\');
        if (!slash) return path;
        *slash = 0;
        std::snprintf(directory, sizeof directory, "%s\\..\\build\\replay-scratch", dll);
        CreateDirectoryA(directory, nullptr);
    }
    if (!g_replay || !path || !(access & GENERIC_WRITE)) return path;
    const char* name = std::strrchr(path, '\\');
    const char* forward = std::strrchr(path, '/');
    if (forward > name) name = forward;
    name = name ? name + 1 : path;
    if (_stricmp(name, "log.html") != 0 && _stricmp(name, "log.txt") != 0) return path;
    std::snprintf(out, MAX_PATH * 2, "%s\\%s", directory, name);
    if (g_log && std::strcmp(last_from, path) != 0) {
        std::snprintf(last_from, sizeof last_from, "%s", path);
        std::fprintf(g_log, "# io redirect %s -> %s\n", path, out);
        std::fflush(g_log);
    }
    return out;
}

HANDLE WINAPI IoCreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                            DWORD disposition, DWORD flags, HANDLE templ) {
    log_io_call(kIoCreateFileA, _ReturnAddress());
    log_open(kIoCreateFileA, path, access, disposition);
    char scratch[MAX_PATH * 2];
    path = replay_scratch(scratch, path, access);
    return g_api.createFileA(path, access, share, sa, disposition, flags, templ);
}

HANDLE WINAPI IoCreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                            DWORD disposition, DWORD flags, HANDLE templ) {
    log_io_call(kIoCreateFileW, _ReturnAddress());
    char text[MAX_PATH] = {};
    if (path) WideCharToMultiByte(CP_ACP, 0, path, -1, text, sizeof text, nullptr, nullptr);
    log_open(kIoCreateFileW, text, access, disposition);
    return g_api.createFileW(path, access, share, sa, disposition, flags, templ);
}

DWORD WINAPI IoGetFileAttributesA(LPCSTR path) {
    log_io_call(kIoGetFileAttributesA, _ReturnAddress());
    log_path(kIoGetFileAttributesA, path);
    return g_api.getFileAttributesA(path);
}

BOOL WINAPI IoDeleteFileA(LPCSTR path) {
    log_io_call(kIoDeleteFileA, _ReturnAddress());
    log_path(kIoDeleteFileA, path);
    return g_api.deleteFileA(path);
}

BOOL WINAPI IoCreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa) {
    log_io_call(kIoCreateDirectoryA, _ReturnAddress());
    log_path(kIoCreateDirectoryA, path);
    return g_api.createDirectoryA(path, sa);
}

DWORD WINAPI IoGetPrivateProfileStringA(LPCSTR app, LPCSTR key, LPCSTR fallback, LPSTR out,
                                        DWORD size, LPCSTR file) {
    log_io_call(kIoGetPrivateProfileStringA, _ReturnAddress());
    char text[3 * MAX_PATH];
    std::snprintf(text, sizeof text, "%s | %s | %s", file ? file : "(null)",
                  app ? app : "(null)", key ? key : "(null)");
    log_path(kIoGetPrivateProfileStringA, text);
    return g_api.getPrivateProfileStringA(app, key, fallback, out, size, file);
}

LONG WINAPI IoRegOpenKeyA(HKEY root, LPCSTR sub, PHKEY out) {
    log_io_call(kIoRegOpenKeyA, _ReturnAddress());
    log_path(kIoRegOpenKeyA, sub ? sub : "(null)");
    return g_api.regOpenKeyA(root, sub, out);
}

LONG WINAPI IoRegOpenKeyExA(HKEY root, LPCSTR sub, DWORD options, REGSAM sam, PHKEY out) {
    log_io_call(kIoRegOpenKeyExA, _ReturnAddress());
    log_path(kIoRegOpenKeyExA, sub ? sub : "(null)");
    return g_api.regOpenKeyExA(root, sub, options, sam, out);
}

void set_game_dir() {
    char exe[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) return;
    char* slash = std::strrchr(exe, '\\');
    if (slash) *slash = 0;
    std::snprintf(g_game_dir, sizeof g_game_dir, "%s", exe);
}

bool path_value(LPCSTR name) {
    return name && (!_stricmp(name, "InstallDirectory"));
}

LONG answer_game_dir(LPCSTR name, LONG result, DWORD capacity, LPDWORD type, LPBYTE data,
                     LPDWORD size) {
    if (!g_game_dir_self || result || !g_game_dir[0] || !path_value(name)) return result;
    if (!size) return result;
    size_t need = std::strlen(g_game_dir) + 1;
    if (!data) {
        if (type) *type = REG_SZ;
        *size = (DWORD)need;
        return result;
    }
    if (capacity < need) return result;
    std::memcpy(data, g_game_dir, need);
    if (type) *type = REG_SZ;
    *size = (DWORD)need;
    if (g_log) {
        std::fprintf(g_log, "# io game_dir %s %s\n", name, g_game_dir);
        std::fflush(g_log);
    }
    return result;
}

LONG WINAPI IoRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type,
                               LPBYTE data, LPDWORD size) {
    log_io_call(kIoRegQueryValueExA, _ReturnAddress());
    log_path(kIoRegQueryValueExA, name ? name : "(default)");
    DWORD capacity = size ? *size : 0;
    LONG result = g_api.regQueryValueExA(key, name, reserved, type, data, size);
    return answer_game_dir(name, result, capacity, type, data, size);
}

LONG WINAPI IoRegEnumKeyExA(HKEY key, DWORD index, LPSTR name, LPDWORD size, LPDWORD reserved,
                            LPSTR class_name, LPDWORD class_size, PFILETIME written) {
    log_io_call(kIoRegEnumKeyExA, _ReturnAddress());
    LONG result = g_api.regEnumKeyExA(key, index, name, size, reserved, class_name, class_size,
                                      written);
    log_path(kIoRegEnumKeyExA, name ? name : "(none)");
    return result;
}

LONG WINAPI IoRegQueryInfoKeyA(HKEY key, LPSTR class_name, LPDWORD class_len, LPDWORD reserved,
                               LPDWORD subkeys, LPDWORD max_subkey, LPDWORD max_class,
                               LPDWORD values, LPDWORD max_value, LPDWORD max_data,
                               LPDWORD security, PFILETIME written) {
    log_io_call(kIoRegQueryInfoKeyA, _ReturnAddress());
    return g_api.regQueryInfoKeyA(key, class_name, class_len, reserved, subkeys, max_subkey,
                                  max_class, values, max_value, max_data, security, written);
}

HANDLE WINAPI IoCreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect,
                                   DWORD high, DWORD low, LPCSTR name) {
    log_io_call(kIoCreateFileMappingA, _ReturnAddress());
    log_path(kIoCreateFileMappingA, name ? name : "(unnamed)");
    return g_api.createFileMappingA(file, sa, protect, high, low, name);
}

struct DInput8Wrap {
    void** vtbl;
    void* real;
};

const unsigned char kReplayGuid[16] = {0x54, 0x4a, 0x52, 0x65, 0x70, 0x6c, 0x61, 0x79,
                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01};

HRESULT __stdcall ProxyDeviceSetDataFormat(DeviceWrap* self, const void* format);
HRESULT __stdcall ProxyDeviceGetDeviceState(DeviceWrap* self, DWORD size, void* data);
HRESULT __stdcall ProxyDeviceSetCooperativeLevel(DeviceWrap* self, HWND window, DWORD flags);
HRESULT __stdcall ProxyDeviceGetCapabilities(DeviceWrap* self, void* caps);
HRESULT __stdcall ProxyDeviceAcquire(DeviceWrap* self);
HRESULT __stdcall ProxyDeviceUnacquire(DeviceWrap* self);
HRESULT __stdcall ProxyDevicePoll(DeviceWrap* self);
HRESULT __stdcall ProxyDInputCreateDevice(DInput8Wrap* self, const void* guid, void** out,
                                          void* outer);
HRESULT __stdcall ProxyDInputEnumDevices(DInput8Wrap* self, DWORD devtype, void* callback,
                                         void* ref, DWORD flags);
HRESULT __stdcall SyntheticSetProperty(DeviceWrap* self, unsigned property, const void* header);
HRESULT __stdcall SyntheticEnumObjects(DeviceWrap* self, void* callback, void* ref,
                                       DWORD flags);
HRESULT __stdcall SyntheticRelease(DeviceWrap* self);

HRESULT __stdcall SyntheticUnmeasured(DeviceWrap* self) {
    (void)self;
    return 0;
}

void* wrap_device(void* real) {
    auto* w = new DeviceWrap;
    w->real = real;
    w->role = 0;
    w->synthetic = 0;
    w->vtbl = new void*[kIoDevSlots];
    auto* code = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, kIoDevSlots * kThunk, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code) return real;
    void** real_vtbl = *(void***)real;
    unsigned char* q = code;
    for (unsigned slot = 0; slot < kIoDevSlots; ++slot) {
        w->vtbl[slot] = q;
        q = emit_io_dev_thunk(q, w, slot, real, &real_vtbl[slot]);
    }
    w->vtbl[kIoSetDataFormat] = (void*)&ProxyDeviceSetDataFormat;
    w->vtbl[kIoGetDeviceState] = (void*)&ProxyDeviceGetDeviceState;
    w->vtbl[kIoSetCooperativeLevel] = (void*)&ProxyDeviceSetCooperativeLevel;
    w->vtbl[kIoAcquire] = (void*)&ProxyDeviceAcquire;
    w->vtbl[kIoUnacquire] = (void*)&ProxyDeviceUnacquire;
    w->vtbl[kIoPoll] = (void*)&ProxyDevicePoll;
    return w;
}

void* wrap_dinput8(void* real) {
    auto* w = new DInput8Wrap;
    w->real = real;
    w->vtbl = new void*[kIoDinput8Slots];
    auto* code = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, kIoDinput8Slots * kThunk, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code) return real;
    void** real_vtbl = *(void***)real;
    unsigned char* q = code;
    for (unsigned slot = 0; slot < kIoDinput8Slots; ++slot) {
        w->vtbl[slot] = q;
        q = emit_io_thunk(q, kIoDinput8 + slot, real, &real_vtbl[slot]);
    }
    w->vtbl[kIoCreateDevice] = (void*)&ProxyDInputCreateDevice;
    w->vtbl[kIoEnumDevices] = (void*)&ProxyDInputEnumDevices;
    return w;
}

void* wrap_synthetic_device() {
    auto* w = new DeviceWrap;
    w->real = nullptr;
    w->role = 2;
    w->synthetic = 1;
    w->vtbl = new void*[kIoDevSlots];
    for (unsigned slot = 0; slot < kIoDevSlots; ++slot) w->vtbl[slot] = (void*)&SyntheticUnmeasured;
    w->vtbl[kIoRelease] = (void*)&SyntheticRelease;
    w->vtbl[kIoGetCapabilities] = (void*)&ProxyDeviceGetCapabilities;
    w->vtbl[kIoEnumObjects] = (void*)&SyntheticEnumObjects;
    w->vtbl[kIoSetProperty] = (void*)&SyntheticSetProperty;
    w->vtbl[kIoAcquire] = (void*)&ProxyDeviceAcquire;
    w->vtbl[kIoUnacquire] = (void*)&ProxyDeviceUnacquire;
    w->vtbl[kIoPoll] = (void*)&ProxyDevicePoll;
    w->vtbl[kIoGetDeviceState] = (void*)&ProxyDeviceGetDeviceState;
    w->vtbl[kIoSetDataFormat] = (void*)&ProxyDeviceSetDataFormat;
    w->vtbl[kIoSetCooperativeLevel] = (void*)&ProxyDeviceSetCooperativeLevel;
    return w;
}

HRESULT __stdcall ProxyDeviceSetDataFormat(DeviceWrap* self, const void* format) {
    uint32_t size = 0;
    if (format) std::memcpy(&size, static_cast<const unsigned char*>(format) + 12, 4);
    self->role = size == 256 ? 1 : size == 272 ? 2 : size == 16 ? 3 : 0;
    log_io_dev(self, kIoSetDataFormat, _ReturnAddress());
    HRESULT hr = 0;
    if (!self->synthetic) {
        typedef HRESULT(__stdcall * Fn)(void*, const void*);
        Fn real = (Fn)(*(void***)self->real)[kIoSetDataFormat];
        hr = real(self->real, format);
    }
    log_hr(self->role, kHrSetDataFormat, hr);
    if (g_log) {
        EnterCriticalSection(&g_lock);
        std::fprintf(g_log, "# io dformat size=%u role=%s\n", size, io_role(self->role));
        std::fflush(g_log);
        LeaveCriticalSection(&g_lock);
    }
    return hr;
}

void log_joy_state();

HRESULT replay_joy_state(DWORD size, void* data) {
    if (!data) {
        log_joy_state();
        return 0;
    }
    unsigned char* bytes = static_cast<unsigned char*>(data);
    std::memset(bytes, 0, size < 272 ? size : 272);
    if (size >= 0x24) static_cast<uint32_t*>(data)[8] = (uint32_t)g_state.pov;
    for (unsigned i = 0; i < 8 && 0x30 + i < size; ++i) {
        bytes[0x30 + i] = (g_state.joy_buttons >> i) & 1 ? 0x80 : 0;
    }
    log_joy_state();
    return 0;
}

HRESULT replay_mouse_state(DWORD size, void* data) {
    if (!data) return 0;
    long dx = 0;
    long dy = 0;
    replay::take_mouse(&g_state, &dx, &dy);
    if (size >= 8) {
        static_cast<int32_t*>(data)[0] = (int32_t)dx;
        static_cast<int32_t*>(data)[1] = (int32_t)dy;
    }
    if (size >= 16) {
        static_cast<int32_t*>(data)[2] = 0;
        for (unsigned i = 0; i < 4; ++i) {
            static_cast<unsigned char*>(data)[12 + i] = (g_state.buttons >> i) & 1 ? 0x80 : 0;
        }
    }
    if (g_log) {
        static unsigned printed;
        static long last_dx;
        static long last_dy;
        static unsigned last_buttons = 0xffffffffu;
        if (printed < 40 || dx != last_dx || dy != last_dy || g_state.buttons != last_buttons) {
            ++printed;
            last_dx = dx;
            last_dy = dy;
            last_buttons = g_state.buttons;
            EnterCriticalSection(&g_lock);
            std::fprintf(g_log, "# replay state mouse dx=%ld dy=%ld btn=%u\n", dx, dy,
                         g_state.buttons);
            std::fflush(g_log);
            LeaveCriticalSection(&g_lock);
        }
    }
    return 0;
}

HRESULT replay_keyboard_state(DWORD size, void* data) {
    if (!data) return 0;
    std::memcpy(data, g_state.keys, size < 256 ? size : 256);
    return 0;
}

void log_joy_state() {
    if (!g_log) return;
    static unsigned printed;
    static long last_pov = 1;
    static unsigned last_buttons = 0xffffffffu;
    if (printed >= 40 && g_state.pov == last_pov && g_state.joy_buttons == last_buttons) return;
    ++printed;
    last_pov = g_state.pov;
    last_buttons = g_state.joy_buttons;
    EnterCriticalSection(&g_lock);
    std::fprintf(g_log, "# replay state joystick pov=%ld btn=%08X frame=%u\n", g_state.pov,
                 g_state.joy_buttons, g_presents);
    std::fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

HRESULT __stdcall ProxyDeviceGetDeviceState(DeviceWrap* self, DWORD size, void* data) {
    log_io_dev(self, kIoGetDeviceState, _ReturnAddress());
    if (g_replay) {
        if (self->role == 3) return replay_mouse_state(size, data);
        if (self->role == 1) return replay_keyboard_state(size, data);
        if (self->role == 2 && self->synthetic) return replay_joy_state(size, data);
    }
    typedef HRESULT(__stdcall * Fn)(void*, DWORD, void*);
    Fn real = (Fn)(*(void***)self->real)[kIoGetDeviceState];
    HRESULT hr = real(self->real, size, data);
    if (self->role == 3) read_cursor();
    return hr;
}

HRESULT __stdcall ProxyDeviceGetCapabilities(DeviceWrap* self, void* caps) {
    log_io_dev(self, kIoGetCapabilities, _ReturnAddress());
    if (!caps) return 0;
    if (self->synthetic) {
        auto* dwords = static_cast<uint32_t*>(caps);
        unsigned size = dwords[0] >= 0x2c ? 0x2c : dwords[0];
        std::memset(caps, 0, size);
        dwords[0] = 0x2c;
        dwords[2] = 0x0102;
        dwords[3] = 2;
        dwords[4] = 8;
        dwords[5] = 1;
        return 0;
    }
    typedef HRESULT(__stdcall * Fn)(void*, void*);
    Fn real = (Fn)(*(void***)self->real)[kIoGetCapabilities];
    return real(self->real, caps);
}

HRESULT __stdcall ProxyDeviceSetCooperativeLevel(DeviceWrap* self, HWND window, DWORD flags) {
    log_io_dev(self, kIoSetCooperativeLevel, _ReturnAddress());
    HRESULT hr = 0;
    if (!self->synthetic) {
        typedef HRESULT(__stdcall * Fn)(void*, HWND, DWORD);
        Fn real = (Fn)(*(void***)self->real)[kIoSetCooperativeLevel];
        hr = real(self->real, window, flags);
    }
    log_hr(self->role, kHrSetCooperativeLevel, hr);
    return hr;
}

HRESULT __stdcall ProxyDeviceAcquire(DeviceWrap* self) {
    log_io_dev(self, kIoAcquire, _ReturnAddress());
    if (g_replay) {
        log_hr(self->role, kHrAcquire, 0);
        return 0;
    }
    typedef HRESULT(__stdcall * Fn)(void*);
    Fn real = (Fn)(*(void***)self->real)[kIoAcquire];
    HRESULT hr = real(self->real);
    log_hr(self->role, kHrAcquire, hr);
    return hr;
}

HRESULT __stdcall ProxyDeviceUnacquire(DeviceWrap* self) {
    log_io_dev(self, kIoUnacquire, _ReturnAddress());
    if (g_replay) return 0;
    if (self->synthetic) return 0;
    typedef HRESULT(__stdcall * Fn)(void*);
    Fn real = (Fn)(*(void***)self->real)[kIoUnacquire];
    return real(self->real);
}

HRESULT __stdcall ProxyDevicePoll(DeviceWrap* self) {
    log_io_dev(self, kIoPoll, _ReturnAddress());
    if (g_replay) return 0;
    if (self->synthetic) return 0;
    typedef HRESULT(__stdcall * Fn)(void*);
    Fn real = (Fn)(*(void***)self->real)[kIoPoll];
    return real(self->real);
}

HRESULT __stdcall SyntheticSetProperty(DeviceWrap* self, unsigned property, const void* header) {
    log_io_dev(self, kIoSetProperty, _ReturnAddress());
    (void)property;
    (void)header;
    return 0;
}

HRESULT __stdcall SyntheticEnumObjects(DeviceWrap* self, void* callback, void* ref,
                                       DWORD flags) {
    log_io_dev(self, kIoEnumObjects, _ReturnAddress());
    (void)callback;
    (void)ref;
    (void)flags;
    return 0;
}

HRESULT __stdcall SyntheticRelease(DeviceWrap* self) {
    log_io_dev(self, kIoRelease, _ReturnAddress());
    (void)self;
    return 0;
}

HRESULT __stdcall ProxyDInputEnumDevices(DInput8Wrap* self, DWORD devtype, void* callback,
                                         void* ref, DWORD flags) {
    log_io_call(kIoDinput8 + kIoEnumDevices, _ReturnAddress());
    if (!g_replay) {
        typedef HRESULT(__stdcall * Fn)(void*, DWORD, void*, void*, DWORD);
        Fn real = (Fn)(*(void***)self->real)[kIoEnumDevices];
        return real(self->real, devtype, callback, ref, flags);
    }
    if (callback) {

        unsigned char instance[0x438] = {};
        uint32_t size = 0x438;
        std::memcpy(instance, &size, 4);
        std::memcpy(instance + 4, kReplayGuid, sizeof kReplayGuid);
        std::memcpy(instance + 0x14, kReplayGuid, sizeof kReplayGuid);
        uint32_t type = 0x0102;
        std::memcpy(instance + 0x24, &type, 4);
        typedef BOOL(__stdcall * CallbackFn)(const void*, void*);
        ((CallbackFn)callback)(instance, ref);
    }
    if (g_log) {
        EnterCriticalSection(&g_lock);
        std::fprintf(g_log, "# replay joy enum devtype=%u\n", (unsigned)devtype);
        std::fflush(g_log);
        LeaveCriticalSection(&g_lock);
    }
    return 0;
}

HRESULT __stdcall ProxyDInputCreateDevice(DInput8Wrap* self, const void* guid, void** out,
                                          void* outer) {
    log_io_call(kIoDinput8 + kIoCreateDevice, _ReturnAddress());
    if (g_replay && guid && !std::memcmp(guid, kReplayGuid, sizeof kReplayGuid)) {
        if (!out) return 0x80070057u;
        *out = wrap_synthetic_device();
        log_hr(0, kHrCreateDevice, 0);
        return 0;
    }
    typedef HRESULT(__stdcall * Fn)(void*, const void*, void**, void*);
    Fn real = (Fn)(*(void***)self->real)[kIoCreateDevice];
    HRESULT hr = real(self->real, guid, out, outer);
    log_hr(0, kHrCreateDevice, hr);
    if (SUCCEEDED(hr) && out && *out) *out = wrap_device(*out);
    return hr;
}

HRESULT WINAPI IoDirectInput8Create(HINSTANCE instance, DWORD version, const void* riid,
                                    void** out, void* outer) {
    log_io_call(kIoDinput8Create, _ReturnAddress());
    HRESULT hr = g_api.dinput8Create(instance, version, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out) *out = wrap_dinput8(*out);
    return hr;
}

void* patch_import_walk(HMODULE module, const char* dll, const char* name, void* replacement) {
    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    const unsigned image_size = nt->OptionalHeader.SizeOfImage;
    for (; imports->Name; ++imports) {
        if (imports->Name >= image_size) continue;
        if (_stricmp(reinterpret_cast<const char*>(base + imports->Name), dll) != 0) continue;
        auto** slots = reinterpret_cast<void**>(base + imports->FirstThunk);
        auto* names = imports->OriginalFirstThunk
                          ? reinterpret_cast<uint32_t*>(base + imports->OriginalFirstThunk)
                          : reinterpret_cast<uint32_t*>(slots);
        for (unsigned i = 0; names[i]; ++i) {
            if (names[i] & 0x80000000u) continue;
            if (names[i] >= image_size) continue;
            if (std::strcmp(reinterpret_cast<const char*>(base + names[i] + 2), name) != 0) {
                continue;
            }
            void* previous = slots[i];
            if (previous == replacement) return previous;
            DWORD old = 0;
            if (!VirtualProtect(&slots[i], sizeof(void*), PAGE_READWRITE, &old)) return nullptr;
            slots[i] = replacement;
            VirtualProtect(&slots[i], sizeof(void*), old, &old);
            if (g_log) {
                EnterCriticalSection(&g_lock);
                std::fprintf(g_log, "# io iat %s %s %08X\n", dll, name,
                             (unsigned)(uintptr_t)&slots[i]);
                std::fflush(g_log);
                LeaveCriticalSection(&g_lock);
            }
            return previous;
        }
    }
    return nullptr;
}

void* patch_import(HMODULE module, const char* dll, const char* name, void* replacement) {
    void* previous = nullptr;
    __try {
        previous = patch_import_walk(module, dll, name, replacement);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        previous = nullptr;
        if (g_log) {
            std::fprintf(g_log, "# io iat fault %s %s\n", dll, name);
            std::fflush(g_log);
        }
    }
    return previous;
}

void* patch_or_log(const char* dll, const char* name, void* replacement) {
    void* real = patch_import(GetModuleHandleA(nullptr), dll, name, replacement);
    if (!real && g_log) {
        EnterCriticalSection(&g_lock);
        std::fprintf(g_log, "# io iat missing %s %s\n", dll, name);
        std::fflush(g_log);
        LeaveCriticalSection(&g_lock);
    }
    return real;
}

void* const kFocusReplacements[] = {(void*)&IoSetForegroundWindow, (void*)&IoBringWindowToTop,
                                    (void*)&IoSetActiveWindow,    (void*)&IoSetFocus,
                                    (void*)&IoShowWindow,         (void*)&IoSetWindowPos,
                                    (void*)&IoCreateWindowExA,    (void*)&IoCreateWindowExW};
const char* const kFocusCalls[] = {"SetForegroundWindow", "BringWindowToTop", "SetActiveWindow",
                                   "SetFocus", "ShowWindow", "SetWindowPos",
                                   "CreateWindowExA", "CreateWindowExW"};

void patch_focus_slots() {

    static const char* const kModules[] = {"mshtml.dll",  "ieframe.dll", "shdocvw.dll",
                                           "atl71.dll",   "urlmon.dll",  "wininet.dll",
                                           "ole32.dll",   "oleaut32.dll", "comctl32.dll",
                                           "shlwapi.dll", "d3d9.dll",    "d3d8to9.dll",
                                           "dinput8.dll", "winmm.dll",
                                           "dsound.dll",  "fmod.dll",    "msvfw32.dll",
                                           "avifil32.dll"};
    unsigned patched = 0;
    for (unsigned m = 0; m < sizeof kModules / sizeof kModules[0]; ++m) {
        HMODULE module = GetModuleHandleA(kModules[m]);
        if (!module) continue;
        for (unsigned n = 0; n < sizeof kFocusCalls / sizeof kFocusCalls[0]; ++n) {
            if (patch_import(module, "USER32.dll", kFocusCalls[n], kFocusReplacements[n])) {
                ++patched;
            }
        }
    }
    static unsigned reported;
    if (g_log && patched != reported) {
        reported = patched;
        std::fprintf(g_log, "# guard slots=%u tick=%u\n", patched, GetTickCount());
        std::fflush(g_log);
    }
}

DWORD WINAPI focus_guard_thread(LPVOID) {
    if (g_log) {
        std::fprintf(g_log, "# guard armed\n");
        std::fflush(g_log);
    }
    for (unsigned tick = 0; tick < 480; ++tick) {
        patch_focus_slots();
        Sleep(250);
    }
    return 0;
}

void arm_io() {
    if (g_io_armed) return;
    if (!g_api.timeGetTime) {
        g_api.timeGetTime =
            (TimeGetTimeFn)patch_or_log("WINMM.dll", "timeGetTime", (void*)&IoTimeGetTime);
    }
    if (!g_api.qpc) {
        g_api.qpc = (QueryPerformanceCounterFn)patch_or_log(
            "KERNEL32.dll", "QueryPerformanceCounter", (void*)&IoQpc);
    }
    if (!g_api.qpf) {
        g_api.qpf = (QueryPerformanceFrequencyFn)patch_or_log(
            "KERNEL32.dll", "QueryPerformanceFrequency", (void*)&IoQpf);
    }
    if (!g_api.getTickCount) {
        g_api.getTickCount =
            (GetTickCountFn)patch_or_log("KERNEL32.dll", "GetTickCount", (void*)&IoGetTickCount);
    }
    if (!g_api.getLocalTime) {
        g_api.getLocalTime =
            (GetLocalTimeFn)patch_or_log("KERNEL32.dll", "GetLocalTime", (void*)&IoGetLocalTime);
    }
    if (!g_api.systemTimeAsFileTime) {
        g_api.systemTimeAsFileTime = (GetSystemTimeAsFileTimeFn)patch_or_log(
            "KERNEL32.dll", "GetSystemTimeAsFileTime", (void*)&IoGetSystemTimeAsFileTime);
    }
    if (!g_api.sleep) {
        g_api.sleep = (SleepFn)patch_or_log("KERNEL32.dll", "Sleep", (void*)&IoSleep);
    }
    if (!g_api.getCursorPos) {
        g_api.getCursorPos =
            (GetCursorPosFn)patch_or_log("USER32.dll", "GetCursorPos", (void*)&IoGetCursorPos);
    }
    if (!g_api.setCursorPos) {
        g_api.setCursorPos =
            (SetCursorPosFn)patch_or_log("USER32.dll", "SetCursorPos", (void*)&IoSetCursorPos);
    }
    if (!g_api.getForegroundWindow) {
        g_api.getForegroundWindow = (GetForegroundWindowFn)patch_or_log(
            "USER32.dll", "GetForegroundWindow", (void*)&IoGetForegroundWindow);
    }
    if (!g_api.getFocus) {
        g_api.getFocus =
            (GetFocusFn)patch_or_log("USER32.dll", "GetFocus", (void*)&IoGetFocus);
    }
    if (!g_api.isIconic) {
        g_api.isIconic =
            (IsIconicFn)patch_or_log("USER32.dll", "IsIconic", (void*)&IoIsIconic);
    }
    if (!g_api.setForegroundWindow) {
        g_api.setForegroundWindow = (SetForegroundWindowFn)patch_or_log(
            "USER32.dll", "SetForegroundWindow", (void*)&IoSetForegroundWindow);
    }
    if (!g_api.showWindow) {
        g_api.showWindow =
            (ShowWindowFn)patch_or_log("USER32.dll", "ShowWindow", (void*)&IoShowWindow);
    }
    if (!g_api.createWindowExA) {
        g_api.createWindowExA = (CreateWindowExAFn)patch_or_log(
            "USER32.dll", "CreateWindowExA", (void*)&IoCreateWindowExA);
    }
    if (!g_api.setWindowPos) {
        g_api.setWindowPos =
            (SetWindowPosFn)patch_or_log("USER32.dll", "SetWindowPos", (void*)&IoSetWindowPos);
    }
    if (!g_api.bringWindowToTop) {
        g_api.bringWindowToTop = (BringWindowToTopFn)patch_or_log(
            "USER32.dll", "BringWindowToTop", (void*)&IoBringWindowToTop);
    }
    if (!g_api.findWindowA) {
        g_api.findWindowA =
            (FindWindowAFn)patch_or_log("USER32.dll", "FindWindowA", (void*)&IoFindWindowA);
    }
    if (!g_api.setFocus) {
        g_api.setFocus = (SetFocusFn)patch_or_log("USER32.dll", "SetFocus", (void*)&IoSetFocus);
    }
    if (!g_api.setActiveWindow) {
        g_api.setActiveWindow = (SetActiveWindowFn)patch_or_log("USER32.dll", "SetActiveWindow",
                                                               (void*)&IoSetActiveWindow);
    }
    if (!g_api.setWindowLongA) {
        g_api.setWindowLongA = (SetWindowLongAFn)patch_or_log(
            "USER32.dll", "SetWindowLongA", (void*)&IoSetWindowLongA);
    }
    if (!g_api.peekMessage) {
        g_api.peekMessage =
            (PeekMessageFn)patch_or_log("USER32.dll", "PeekMessageA", (void*)&IoPeekMessage);
    }
    if (!g_api.getMessage) {
        g_api.getMessage =
            (GetMessageAFn)patch_or_log("USER32.dll", "GetMessageA", (void*)&IoGetMessage);
    }
    if (!g_api.dinput8Create) {
        g_api.dinput8Create = (DirectInput8CreateFn)patch_or_log(
            "DINPUT8.dll", "DirectInput8Create", (void*)&IoDirectInput8Create);
    }

    if (!g_api.createFileA) {
        g_api.createFileA =
            (CreateFileAFn)patch_or_log("KERNEL32.dll", "CreateFileA", (void*)&IoCreateFileA);
    }
    if (!g_api.createFileW) {
        g_api.createFileW =
            (CreateFileWFn)patch_or_log("KERNEL32.dll", "CreateFileW", (void*)&IoCreateFileW);
    }
    if (!g_api.getFileAttributesA) {
        g_api.getFileAttributesA = (GetFileAttributesAFn)patch_or_log(
            "KERNEL32.dll", "GetFileAttributesA", (void*)&IoGetFileAttributesA);
    }
    if (!g_api.deleteFileA) {
        g_api.deleteFileA =
            (DeleteFileAFn)patch_or_log("KERNEL32.dll", "DeleteFileA", (void*)&IoDeleteFileA);
    }
    if (!g_api.createDirectoryA) {
        g_api.createDirectoryA = (CreateDirectoryAFn)patch_or_log(
            "KERNEL32.dll", "CreateDirectoryA", (void*)&IoCreateDirectoryA);
    }
    if (!g_api.getPrivateProfileStringA) {
        g_api.getPrivateProfileStringA = (GetPrivateProfileStringAFn)patch_or_log(
            "KERNEL32.dll", "GetPrivateProfileStringA", (void*)&IoGetPrivateProfileStringA);
    }
    if (!g_api.createFileMappingA) {
        g_api.createFileMappingA = (CreateFileMappingAFn)patch_or_log(
            "KERNEL32.dll", "CreateFileMappingA", (void*)&IoCreateFileMappingA);
    }
    if (!g_api.regOpenKeyA) {
        g_api.regOpenKeyA =
            (RegOpenKeyAFn)patch_or_log("ADVAPI32.dll", "RegOpenKeyA", (void*)&IoRegOpenKeyA);
    }
    if (!g_api.regOpenKeyExA) {
        g_api.regOpenKeyExA = (RegOpenKeyExAFn)patch_or_log("ADVAPI32.dll", "RegOpenKeyExA",
                                                            (void*)&IoRegOpenKeyExA);
    }
    if (!g_api.regQueryValueExA) {
        g_api.regQueryValueExA = (RegQueryValueExAFn)patch_or_log(
            "ADVAPI32.dll", "RegQueryValueExA", (void*)&IoRegQueryValueExA);
    }
    if (!g_api.regEnumKeyExA) {
        g_api.regEnumKeyExA = (RegEnumKeyExAFn)patch_or_log("ADVAPI32.dll", "RegEnumKeyExA",
                                                            (void*)&IoRegEnumKeyExA);
    }
    if (!g_api.regQueryInfoKeyA) {
        g_api.regQueryInfoKeyA = (RegQueryInfoKeyAFn)patch_or_log(
            "ADVAPI32.dll", "RegQueryInfoKeyA", (void*)&IoRegQueryInfoKeyA);
    }
    if (!g_api.coCreateInstance) {
        g_api.coCreateInstance = (CoCreateInstanceFn)patch_or_log(
            "ole32.dll", "CoCreateInstance", (void*)&IoCoCreateInstance);
    }
    g_io_armed = g_api.timeGetTime && g_api.qpc && g_api.qpf && g_api.getTickCount &&
                 g_api.getLocalTime && g_api.systemTimeAsFileTime && g_api.sleep &&
                 g_api.getCursorPos && g_api.setCursorPos && g_api.getForegroundWindow &&
                 g_api.getFocus && g_api.isIconic && g_api.dinput8Create;
    if (g_io_armed && g_log) {
        EnterCriticalSection(&g_lock);
        std::fprintf(g_log, "# io armed\n");
        std::fflush(g_log);
        LeaveCriticalSection(&g_lock);
    }
}

unsigned char* emit_thunk(unsigned char* q, unsigned iface, unsigned slot, void* real,
                          void** slot_address, bool record) {
    *q++ = 0x60;
    *q++ = 0x68; store32(q, iface); q += 4;
    *q++ = 0x68; store32(q, slot); q += 4;
    *q++ = 0xff; *q++ = 0x74; *q++ = 0x24; *q++ = 0x28;
    *q++ = 0xe8;
    unsigned char* const after = q + 4;
    q += 4;
    *q++ = 0x83; *q++ = 0xc4; *q++ = 0x0c;
    *q++ = 0x61;
    if (record) {
        // cdecl pushes right to left: first the argument block, then slot, then iface.

        *q++ = 0x60;
        *q++ = 0x8d; *q++ = 0x44; *q++ = 0x24; *q++ = 0x28;
        *q++ = 0x50;
        *q++ = 0x68; store32(q, slot); q += 4;
        *q++ = 0x68; store32(q, iface); q += 4;
        *q++ = 0xe8;
        unsigned char* const back = q + 4;
        q += 4;
        *q++ = 0x83; *q++ = 0xc4; *q++ = 0x0c;
        *q++ = 0x61;
        int32_t rel2 = (int32_t)((intptr_t)&record_args - (intptr_t)back);
        std::memcpy(back - 4, &rel2, 4);
    }
    *q++ = 0xc7; *q++ = 0x44; *q++ = 0x24; *q++ = 0x04;
    store32(q, (uint32_t)(uintptr_t)real);
    q += 4;
    *q++ = 0xff; *q++ = 0x25;
    store32(q, (uint32_t)(uintptr_t)slot_address);
    q += 4;
    int32_t rel = (int32_t)((intptr_t)&log_call - (intptr_t)after);
    std::memcpy(after - 4, &rel, 4);
    return q;
}

uint32_t param32(const void* params, unsigned offset) {
    uint32_t value = 0;
    if (params) std::memcpy(&value, static_cast<const unsigned char*>(params) + offset, 4);
    return value;
}

void log_create_device(const char* backend, unsigned adapter, unsigned type, DWORD behavior,
                       const void* params, HRESULT hr) {
    if (!g_log) return;
    std::fprintf(g_log,
                 "# d3d8 create_device backend=%s hr=%08X adapter=%u devtype=%u behavior=%08X "
                 "back=%ux%u fmt=%u count=%u ms=%u swap=%u windowed=%u autods=%u dsfmt=%u "
                 "flags=%08X refresh=%u interval=%u\n",
                 backend, (unsigned)hr, adapter, type, behavior, param32(params, 0),
                 param32(params, 4), param32(params, 8), param32(params, 12), param32(params, 16),
                 param32(params, 20), param32(params, 28), param32(params, 32), param32(params, 36),
                 param32(params, 40), param32(params, 44), param32(params, 48));
    std::fflush(g_log);
}

void log_surface(const char* backend, const char* role, const unsigned* d) {
    if (!g_log) return;
    std::fprintf(g_log,
                 "# d3d8 surface backend=%s role=%s fmt=%u type=%u usage=%u pool=%u size=%u "
                 "ms=%u w=%u h=%u\n",
                 backend, role, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    std::fflush(g_log);
}

void log_com_surfaces(const char* backend, void* device) {
    if (!g_log || !device) return;
    typedef HRESULT(__stdcall * BackFn)(void*, UINT, unsigned, void**);
    typedef HRESULT(__stdcall * DsFn)(void*, void**);
    typedef HRESULT(__stdcall * DescFn)(void*, void*);
    typedef ULONG(__stdcall * RelFn)(void*);
    void* surface = nullptr;
    unsigned d[8] = {};
    if (SUCCEEDED(((BackFn)(*(void***)device)[16])(device, 0, 0, &surface)) && surface) {
        ((DescFn)(*(void***)surface)[8])(surface, d);
        log_surface(backend, "back", d);
        ((RelFn)(*(void***)surface)[2])(surface);
        surface = nullptr;
    }
    if (SUCCEEDED(((DsFn)(*(void***)device)[33])(device, &surface)) && surface) {
        ((DescFn)(*(void***)surface)[8])(surface, d);
        log_surface(backend, "depth", d);
        ((RelFn)(*(void***)surface)[2])(surface);
    }
}

void log_render_surfaces(const char* backend, render::Device* device) {
    if (!g_log || !device) return;
    render::Surface* surface = nullptr;
    unsigned d[8] = {};
    if (SUCCEEDED(device->GetBackBuffer(0, 0, &surface)) && surface) {
        surface->GetDesc(d);
        log_surface(backend, "back", d);
        surface->Release();
    }
    surface = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&surface)) && surface) {
        surface->GetDesc(d);
        log_surface(backend, "depth", d);
        surface->Release();
    }
}

void arm_capture(void* device, const void* params, HWND focus) {
    g_device = device;
    g_windowed = param32(params, 28) != 0;
    if (focus) {
        RECT client{};
        POINT origin{0, 0};
        GetClientRect(focus, &client);
        ClientToScreen(focus, &origin);
        g_client = {origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    }
    if (g_log) {
        std::fprintf(g_log,
                     "# capture device width=%u height=%u format=%u windowed=%u hwnd=%08X "
                     "client=%dx%d at %d,%d\n",
                     param32(params, 0), param32(params, 4), param32(params, 8), g_windowed,
                     (unsigned)(uintptr_t)focus, (int)(g_client.right - g_client.left),
                     (int)(g_client.bottom - g_client.top), (int)g_client.left,
                     (int)g_client.top);
        std::fflush(g_log);
    }
}

struct FramePixels {
    void* image;
    void* back;
    const unsigned char* bits;
    int32_t pitch;
    unsigned width;
    unsigned height;
    unsigned format;
};

bool lock_frame(FramePixels* frame) {
    *frame = FramePixels{};
    auto slot = [](void* object, unsigned index) { return (*(void***)object)[index]; };
    void* back = nullptr;
    HRESULT hr = ((HRESULT(__stdcall*)(void*, unsigned, unsigned, void**))slot(
                      g_device, kBackBuffer))(g_device, 0, 0, &back);
    if (SUCCEEDED(hr) && back) {
        uint32_t desc[8] = {};
        hr = ((HRESULT(__stdcall*)(void*, void*))slot(back, kSurfaceGetDesc))(back, desc);
        frame->format = desc[0];
        frame->width = desc[6];
        frame->height = desc[7];
    }
    void* image = nullptr;
    if (SUCCEEDED(hr) && frame->width && frame->height) {
        hr = ((HRESULT(__stdcall*)(void*, unsigned, unsigned, unsigned, void**))slot(
                  g_device, kCreateImageSurface))(g_device, frame->width, frame->height,
                                                 frame->format, &image);
    }
    if (SUCCEEDED(hr) && image) {
        hr = ((HRESULT(__stdcall*)(void*, void*, const RECT*, unsigned, void*, const POINT*))slot(
                  g_device, kCopyRects))(g_device, back, nullptr, 0, image, nullptr);
    }
    struct Locked {
        int32_t pitch;
        void* bits;
    } locked = {};
    if (SUCCEEDED(hr) && image) {
        hr = ((HRESULT(__stdcall*)(void*, void*, const RECT*, DWORD))slot(
                  image, kSurfaceLockRect))(image, &locked, nullptr, 0);
    }
    if (SUCCEEDED(hr) && image && locked.bits) {
        frame->image = image;
        frame->back = back;
        frame->bits = static_cast<const unsigned char*>(locked.bits);
        frame->pitch = locked.pitch;
        return true;
    }
    if (image) ((ULONG(__stdcall*)(void*))slot(image, kSurfaceRelease))(image);
    if (back) ((ULONG(__stdcall*)(void*))slot(back, kSurfaceRelease))(back);
    return false;
}

void unlock_frame(FramePixels* frame) {
    auto slot = [](void* object, unsigned index) { return (*(void***)object)[index]; };
    if (frame->image) {
        ((HRESULT(__stdcall*)(void*))slot(frame->image, kSurfaceUnlockRect))(frame->image);
        ((ULONG(__stdcall*)(void*))slot(frame->image, kSurfaceRelease))(frame->image);
    }
    if (frame->back) ((ULONG(__stdcall*)(void*))slot(frame->back, kSurfaceRelease))(frame->back);
}

void capture_frame(unsigned n) {
    FramePixels frame = {};
    bool ok = lock_frame(&frame) &&
              bmp::write_24(g_capture_out, frame.width, frame.height, frame.bits, frame.pitch);
    unlock_frame(&frame);
    if (g_log) {

        std::fprintf(g_log, "# capture n=%u hr=%08X path=%s size=%ux%u format=%u\n", n,
                     ok ? 0u : (unsigned)E_FAIL, g_capture_out, frame.width, frame.height,
                     frame.format);
        std::fflush(g_log);
    }
}

bool frame_hash(uint32_t* hash) {
    FramePixels pixels = {};
    if (!lock_frame(&pixels)) return false;
    uint32_t value = 2166136261u;
    for (unsigned y = 0; y < pixels.height; ++y) {
        const unsigned char* row = pixels.bits + (size_t)y * (size_t)pixels.pitch;
        for (unsigned x = 0; x < pixels.width * 4; ++x) {
            value ^= row[x];
            value *= 16777619u;
        }
    }
    unlock_frame(&pixels);
    *hash = value;
    return true;
}

constexpr char kScriptErrorClass[] = "Internet Explorer_TridentDlgFrame";
constexpr char kScriptErrorTitle[] = "Script Error";

BOOL CALLBACK close_script_error(HWND hwnd, LPARAM) {
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner != GetCurrentProcessId()) return TRUE;
    char title[32] = {};
    char name[64] = {};
    GetWindowTextA(hwnd, title, sizeof title);
    GetClassNameA(hwnd, name, sizeof name);
    if (std::strcmp(title, kScriptErrorTitle) != 0 || std::strcmp(name, kScriptErrorClass) != 0) {
        return TRUE;
    }
    PostMessageA(hwnd, WM_CLOSE, 0, 0);
    if (g_log) {
        EnterCriticalSection(&g_lock);
        std::fprintf(g_log, "# silence closed hwnd=%08X\n", (unsigned)(uintptr_t)hwnd);
        std::fflush(g_log);
        LeaveCriticalSection(&g_lock);
    }
    return TRUE;
}

DWORD WINAPI close_script_errors(LPVOID) {
    for (unsigned tick = 0; tick < 6000; ++tick) {
        Sleep(10);
        EnumWindows(close_script_error, 0);
    }
    return 0;
}

void flush_args_locked() {
    for (unsigned i = 0; i < kArgTable; ++i) {
        ArgSet& set = g_arg_table[i];
        if (!set.used || set.printed == set.count) continue;
        std::fprintf(g_log, "# args %u %u %s %08X %08X %08X %08X %08X %08X %u\n", set.iface,
                     set.slot, SLOT_NAMES[set.iface][set.slot], set.v[0], set.v[1], set.v[2],
                     set.v[3], set.v[4], set.v[5], set.count);
        set.printed = set.count;
    }
    std::fflush(g_log);
    g_args_flushed_at = GetTickCount();
}

void add_arg_set(unsigned iface, unsigned slot, const uint32_t* v) {
    if (!g_log || iface >= kIfaceCount || slot >= 128) return;
    uint64_t key = ((uint64_t)slot << 32) | ((uint64_t)v[0] * 0x9e3779b1u);
    unsigned h = (unsigned)((key * 0x9e3779b97f4a7c15ull) >> 53) & (kArgTable - 1);
    EnterCriticalSection(&g_lock);
    unsigned probes = 0;
    while (g_arg_table[h].used && probes < kArgTable) {
        ArgSet& set = g_arg_table[h];
        if (set.iface == iface && set.slot == slot && !std::memcmp(set.v, v, sizeof set.v)) {
            ++set.count;
            break;
        }
        h = (h + 1) & (kArgTable - 1);
        ++probes;
    }
    if (!g_arg_table[h].used) {
        ArgSet& set = g_arg_table[h];
        set.used = true;
        set.iface = iface;
        set.slot = slot;
        set.count = 1;
        set.printed = 0;
        std::memcpy(set.v, v, sizeof set.v);
    } else if (probes >= kArgTable) {
        ++g_dropped;
    }
    if (GetTickCount() - g_args_flushed_at >= 1000) flush_args_locked();
    LeaveCriticalSection(&g_lock);
}

void record_present_params(const void* params) {
    uint32_t v[6] = {param32(params, 28), param32(params, 0), param32(params, 4),
                     param32(params, 8), param32(params, 20), param32(params, 48)};
    add_arg_set(0, kCreateDevice, v);
}

void record_args(unsigned iface, unsigned slot, const uint32_t* p) {
    if (!p) return;
    uint32_t v[6] = {};
    if (iface == 0) {
        if (slot != kCreateDevice || !p) return;
        record_present_params((const void*)(uintptr_t)p[4]);
        return;
    }
    switch (slot) {
        case kCreateTexture:
            for (unsigned i = 0; i < 6; ++i) v[i] = p[i];
            break;
        case kCreateVertexBuffer:
        case kCreateIndexBuffer:
            for (unsigned i = 0; i < 4; ++i) v[i] = p[i];
            break;
        case kSetRenderState:
        case kSetTextureStageState:
        case kSetTransform:
        case kSetViewport:
        case kSetTexture:
        case kSetVertexShader:
        case kSetPixelShader:
        case kDrawPrimitive:
        case kDrawIndexedPrimitive:
        case kDrawPrimitiveUP:
        case kDrawIndexedPrimitiveUP:
        case kClear:
            break;
        default:
            return;
    }
    switch (slot) {
        case kSetRenderState:
        case kSetTransform:
            v[0] = p[0];
            if (slot == kSetRenderState) v[1] = p[1];
            break;
        case kSetTextureStageState:
            v[0] = p[0];
            v[1] = p[1];
            v[2] = p[2];
            break;
        case kSetTexture:
            v[0] = p[0];
            v[1] = p[1] ? 1 : 0;
            break;
        case kSetViewport: {
            const uint32_t* viewport = (const uint32_t*)(uintptr_t)p[0];
            v[0] = viewport ? viewport[2] : 0;
            v[1] = viewport ? viewport[3] : 0;
            break;
        }
        case kSetVertexShader:
        case kSetPixelShader:
            v[0] = p[0];
            break;
        case kClear:
            v[0] = p[2];
            break;
        default:
            if (slot == kDrawPrimitive || slot == kDrawIndexedPrimitive ||
                slot == kDrawPrimitiveUP || slot == kDrawIndexedPrimitiveUP) {
                v[0] = p[0];
            }
            break;
    }
    add_arg_set(iface, slot, v);
}

void arm_args() {
    static const unsigned recorded[] = {
        kCreateTexture,        kCreateVertexBuffer,       kCreateIndexBuffer,
        kClear,                kSetTransform,             kSetViewport,
        kSetRenderState,       kSetTexture,               kSetTextureStageState,
        kDrawPrimitive,        kDrawIndexedPrimitive,     kDrawPrimitiveUP,
        kDrawIndexedPrimitiveUP, kCreateVertexShader,     kSetVertexShader,
        kCreatePixelShader,    kSetPixelShader};
    for (unsigned slot : recorded) ARG_SLOT[1][slot] = true;
}

void* wrap(void* real, unsigned iface) {
    unsigned slots = SLOT_COUNTS[iface];
    Wrapper* w = new Wrapper;
    w->real = real;
    w->vtbl = new void*[slots];
    auto* code = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, slots * kThunk, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code) {

        if (g_log) {
            std::fprintf(g_log, "# thunk allocation failed, interface %u not wrapped\n", iface);
            std::fflush(g_log);
        }
        return real;
    }
    void** real_vtbl = *(void***)real;
    unsigned char* q = code;
    for (unsigned slot = 0; slot < slots; ++slot) {
        w->vtbl[slot] = q;
        q = emit_thunk(q, iface, slot, real, &real_vtbl[slot],
                       g_args && slot < 128 && ARG_SLOT[iface][slot]);
    }
    if (iface == 0) {
        w->vtbl[kCreateDevice] = (void*)&ProxyCreateDevice;
        for (const RootAdapter& adapter : kRootAdapters) w->vtbl[adapter.slot] = adapter.function;
    }
    if (iface == 1) {
        for (const SlotAdapter& adapter : kAdapters) w->vtbl[adapter.slot] = adapter.function;
        w->vtbl[kPresent] = (void*)&ProxyPresent;
    }
    return w;
}

void offscreen_point(int* x, int* y) {
    *x = GetSystemMetrics(SM_XVIRTUALSCREEN) + 64;
    *y = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) + 64;
}

void apply_window_plan(HWND focus, const void* params) {
    g_back_w = param32(params, 0);
    g_back_h = param32(params, 4);
    if (!focus) return;
    if (g_faithful) {

        if (g_background) {
            offscreen_point(&g_background_x, &g_background_y);
            SetWindowPos(focus, nullptr, g_background_x, g_background_y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_log) {
                std::fprintf(g_log, "# window background=1 faithful=1 at %d,%d\n",
                             g_background_x, g_background_y);
                std::fflush(g_log);
            }
        }
        return;
    }
    if (!g_back_w || !g_back_h) return;
    MONITORINFO info = {};
    info.cbSize = sizeof info;
    if (!GetMonitorInfoA(MonitorFromWindow(focus, MONITOR_DEFAULTTOPRIMARY), &info)) return;
    unsigned work_w = (unsigned)(info.rcWork.right - info.rcWork.left);
    unsigned work_h = (unsigned)(info.rcWork.bottom - info.rcWork.top);
    unsigned screen_w = (unsigned)(info.rcMonitor.right - info.rcMonitor.left);
    unsigned screen_h = (unsigned)(info.rcMonitor.bottom - info.rcMonitor.top);
    g_scale_factor = windowmode::factor(g_scale, g_back_w, g_back_h, work_w, work_h);
    windowmode::Plan plan = windowmode::plan(g_window_kind, g_scale_factor, g_back_w, g_back_h,
                                             work_w, work_h, screen_w, screen_h);
    DWORD style = plan.popup ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT outer = {0, 0, (LONG)plan.width, (LONG)plan.height};
    AdjustWindowRect(&outer, style, FALSE);
    LONG w = outer.right - outer.left;
    LONG h = outer.bottom - outer.top;
    int x = info.rcWork.left + ((LONG)work_w - w) / 2;
    int y = info.rcWork.top + ((LONG)work_h - h) / 2;
    if (g_window_kind == windowmode::kBorderless) {
        x = info.rcWork.left;
        y = info.rcWork.top;
    } else if (g_window_kind == windowmode::kDesktop) {
        x = info.rcMonitor.left;
        y = info.rcMonitor.top;
    }
    if (g_background) {
        offscreen_point(&x, &y);
        g_background_x = x;
        g_background_y = y;
    }
    SetWindowLongA(focus, GWL_STYLE, (LONG)style);

    if (plan.popup) {
        static HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        SetClassLongPtrA(focus, GCLP_HBRBACKGROUND, (LONG_PTR)black);
        InvalidateRect(focus, nullptr, TRUE);
    }
    SetWindowPos(focus, nullptr, x, y, w, h, SWP_NOZORDER | SWP_FRAMECHANGED | (g_background ? SWP_NOACTIVATE : 0));
    g_plan_w = plan.width;
    g_plan_h = plan.height;
    g_plan_x = x;
    g_plan_y = y;
    g_plan_outer_w = w;
    g_plan_outer_h = h;
    g_plan_style = style;
    g_scaled = g_window_kind != windowmode::kWindowed || g_scale_factor > 1;
    if (g_log) {
        std::fprintf(g_log,
                     "# window mode=%s scale=%u client=%ux%u backbuffer=%ux%u at %d,%d "
                     "popup=%d\n",
                     g_window_kind == windowmode::kBorderless     ? "borderless"
                     : g_window_kind == windowmode::kDesktop      ? "fullscreen-desktop"
                                                                  : "windowed",
                     g_scale_factor, plan.width, plan.height, g_back_w, g_back_h, (int)x,
                     (int)y, plan.popup ? 1 : 0);
        std::fflush(g_log);
    }
}

void keep_window_plan(unsigned frame) {
    static unsigned forced;
    if (!g_hwnd || frame > 10) return;
    if (g_faithful ? !g_background : (!g_scaled && !g_background)) return;
    if (g_faithful) {
        RECT rect = {};
        if (!GetWindowRect(g_hwnd, &rect)) return;
        if (rect.left == g_background_x && rect.top == g_background_y) return;
        SetWindowPos(g_hwnd, nullptr, g_background_x, g_background_y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (g_log) {
            std::fprintf(g_log, "# window forced frame=%u background=1 at=%d,%d was=%ld,%ld\n",
                         frame, g_background_x, g_background_y, rect.left, rect.top);
            std::fflush(g_log);
        }
        return;
    }
    if (!g_plan_w) return;
    RECT client = {};
    if (!GetClientRect(g_hwnd, &client)) return;
    if ((unsigned)(client.right - client.left) == g_plan_w &&
        (unsigned)(client.bottom - client.top) == g_plan_h) {
        return;
    }
    SetWindowLongA(g_hwnd, GWL_STYLE, (LONG)g_plan_style);
    SetWindowPos(g_hwnd, nullptr, g_plan_x, g_plan_y, g_plan_outer_w, g_plan_outer_h,
                 SWP_NOZORDER | SWP_FRAMECHANGED | (g_background ? SWP_NOACTIVATE : 0));
    ++forced;
    if (g_log) {
        std::fprintf(g_log, "# window forced frame=%u client=%ux%u was=%ldx%ld tries=%u\n",
                     frame, g_plan_w, g_plan_h, client.right - client.left,
                     client.bottom - client.top, forced);
        std::fflush(g_log);
    }
}

HRESULT __stdcall ProxyCreateDevice(void* self, unsigned adapter, unsigned device_type,
                                    HWND focus, DWORD flags, void* params, void** out) {
    log_call(_ReturnAddress(), kCreateDevice, 0);
    Wrapper* w = static_cast<Wrapper*>(self);
    char adapterEnv[16] = {};
    if (GetEnvironmentVariableA("TJ_ADAPTER", adapterEnv, sizeof adapterEnv) > 0) {
        unsigned vendor = (unsigned)std::strtoul(adapterEnv, nullptr, 16);
        HMODULE d3d9 = LoadLibraryA("d3d9.dll");
        typedef void*(__stdcall * CreateFn)(UINT);
        void* root9 = d3d9 ? ((CreateFn)GetProcAddress(d3d9, "Direct3DCreate9"))(32) : nullptr;
        typedef UINT(__stdcall * CountFn)(void*);
        typedef HRESULT(__stdcall * IdFn)(void*, UINT, DWORD, void*);
        typedef ULONG(__stdcall * RelFn)(void*);
        UINT count = 0;
        unsigned found = 0;
        if (root9) {
            count = ((CountFn)(*(void***)root9)[4])(root9);
            found = count;
            for (UINT i = 0; i < count; ++i) {
                unsigned char id[1100] = {};
                if (FAILED(((IdFn)(*(void***)root9)[5])(root9, i, 0, id))) continue;
                unsigned values[2] = {};
                std::memcpy(values, id + 1064, 8);
                if (g_log) {
                    std::fprintf(g_log,
                                 "# d3d9 adapter index=%u vendor=%04X device=%04X name=%.512s "
                                 "display=%.32s\n",
                                 i, values[0], values[1], id + 512, id + 1024);
                    std::fflush(g_log);
                }
                if (values[0] == vendor) found = i;
            }
            ((RelFn)(*(void***)root9)[2])(root9);
        }
        if (found == count) {
            if (g_log) {
                std::fprintf(g_log, "# d3d9 adapter vendor=%04X not found\n", vendor);
                std::fflush(g_log);
            }
            return E_FAIL;
        }
        adapter = found;
    }
    typedef HRESULT(__stdcall * Fn)(void*, unsigned, unsigned, HWND, DWORD, void*, void**);
    Fn real = (Fn)(*(void***)w->real)[kCreateDevice];
    HRESULT hr = real(w->real, adapter, device_type, focus, flags, params, out);
    if (SUCCEEDED(hr) && out && *out) {
        g_hwnd = focus;
        if (g_log) {
            std::fprintf(g_log, "# render attach real=%08X\n", (unsigned)(uintptr_t)*out);
            std::fflush(g_log);
        }
        render::attach(*out);
        apply_window_plan(focus, params);
        if (g_capture) arm_capture(*out, params, focus);
        if (g_snapshot && !g_capture) g_device = *out;
        if (g_args) record_present_params(params);
        log_create_device("legacy", adapter, device_type, flags, params, hr);
        log_com_surfaces("legacy", *out);
        *out = wrap(*out, 1);
        g_device_com = *out;
    }
    return hr;
}

void fixed_cursor_present() {
    static bool done;
    if (done || !g_hwnd) return;
    done = true;
    LPARAM point = (LPARAM)(((unsigned short)replay::kCursorY << 16) |
                            (unsigned short)replay::kCursorX);
    PostMessageA(g_hwnd, WM_MOUSEMOVE, 0, point);
    replay::post_add(&g_posted, (unsigned)point);
    if (g_log) {
        EnterCriticalSection(&g_lock);
        std::fprintf(g_log, "# cursor post frame=%u l=%08X\n", g_presents, (unsigned)point);
        std::fflush(g_log);
        LeaveCriticalSection(&g_lock);
    }
}

HRESULT __stdcall ProxyPresent(void* self, const RECT* source, const RECT* dest, HWND window,
                               const void* dirty) {
    log_call(_ReturnAddress(), kPresent, 1);
    render_trace(kPresent);
    (void)self;
    ++g_render_calls;
    ++g_presents;
    render::frame_number_slot() = g_presents + 1;
    keep_window_plan(g_presents);
    stall::present();
    if (g_replay) replay_present();
    else if (g_fixed_cursor) fixed_cursor_present();
    steer_present();
    if (g_probe) probe_present();
    census_present();
    step_present();
    if (g_watch) watch_present();

    if (g_capture && !g_captured && g_presents == g_capture_at) {
        g_captured = true;
        capture_frame(g_capture_at);
    }
    if (g_draw_state) state_line("# draws f=%u d=%u\n", g_presents, g_draw_state_seen);
    snapshot_present();
    save_present();
    load_present();

    RECT box = {};
    const RECT* use = dest;
    if (g_scaled && !g_faithful) {
        RECT client = {};
        if (g_hwnd && GetClientRect(g_hwnd, &client)) {
            windowmode::Rect r = windowmode::letterbox(g_back_w, g_back_h, client.right,
                                                       client.bottom);

            if (r.left || r.top || r.right != client.right || r.bottom != client.bottom) {
                box.left = r.left;
                box.top = r.top;
                box.right = r.right;
                box.bottom = r.bottom;
                use = &box;
            }
        }
    }
    if (g_log && g_presents % 100 == 0) {
        unsigned counters[6] = {};
        render::device()->counters(counters);
        std::fprintf(g_log,
                     "# render backend=%s device_calls=%u present=%u draws=%u skipped=%u pso=%u "
                     "upload=%u\n",
                     render::backend_name(), g_render_calls, g_presents, counters[0], counters[1],
                     counters[2], counters[3]);
        std::fprintf(g_log, "# render textures uploaded=%u missing=%u\n", counters[3], counters[4]);
        std::fflush(g_log);
    }
    return render::device()->Present(source, use, window, dirty);
}

HMODULE real_dll() {
    static HMODULE module;
    static bool tried;
    if (tried) return module;
    tried = true;
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    GetSystemDirectoryA(directory, MAX_PATH);
    if (backend::translated(g_faithful ? 1 : 0)) {

        char dll[MAX_PATH] = {};
        GetModuleFileNameA(g_module, dll, MAX_PATH);
        char* slash = std::strrchr(dll, '\\');
        if (slash) *slash = 0;
        backend::path(0, dll, directory, path, sizeof path);
    } else {
        backend::path(1, "", directory, path, sizeof path);
    }
    module = LoadLibraryA(path);

    if (g_log) {
        std::fprintf(g_log, "# real %s %s\n", path, module ? "loaded" : "FAILED");
        std::fprintf(g_log, "# backend %s d3d9=%s\n", path,
                     GetModuleHandleA("d3d9.dll") ? "loaded" : "absent");
        std::fflush(g_log);
    }
    return module;
}

void open_log() {
    char path[MAX_PATH * 2] = {};
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    DWORD n = GetEnvironmentVariableA("TJ_D3D8_LOG", path, sizeof path);
    if (n == 0 || n >= sizeof path) {
        char dll[MAX_PATH] = {};
        GetModuleFileNameA(g_module, dll, MAX_PATH);
        char* slash = std::strrchr(dll, '\\');
        if (slash) *slash = 0;
        std::snprintf(path, sizeof path, "%s\\..\\proxy\\d3d8-proxy.log", dll);
    }
    g_log = std::fopen(path, "wb");
    if (!g_log) {
        OutputDebugStringA("d3d8 proxy: log path not writable\n");
        return;
    }

    render::log_sink_slot() = g_log;
    std::fprintf(g_log, "# d3d8 proxy log v1 image_base 0x%08X exe %s\n",
                 (unsigned)(uintptr_t)GetModuleHandleA(nullptr), exe);
    std::fflush(g_log);
}

bool load_replay(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    char text[200 * 128 + 1] = {};
    size_t got = std::fread(text, 1, sizeof text - 1, file);
    bool more = std::fgetc(file) != EOF;
    std::fclose(file);
    if (more) return false;
    text[got] = 0;
    unsigned lines = 0;
    for (size_t i = 0; i < got; ++i) {
        if (text[i] == '\n') ++lines;
    }
    if (got && text[got - 1] != '\n') ++lines;
    if (lines > 200) return false;
    unsigned count = 0;
    if (!replay::parse(text, g_events, kMaxEvents, &count)) return false;
    g_event_count = count;
    g_state.pov = -1;
    return true;
}

void read_capture_env() {
    char at[16] = {};
    if (GetEnvironmentVariableA("TJ_CAPTURE_AT", at, sizeof at) > 0) {
        g_capture_at = (unsigned)std::strtoul(at, nullptr, 10);
    }
    DWORD n = GetEnvironmentVariableA("TJ_CAPTURE_OUT", g_capture_out, sizeof g_capture_out);
    g_capture = g_capture_at > 0 && n > 0 && n < sizeof g_capture_out;
    char silent[8] = {};
    g_silence = GetEnvironmentVariableA("TJ_SILENT_SCRIPT_ERRORS", silent, sizeof silent) > 0 &&
                silent[0] == '1';
    char args[8] = {};
    g_args = GetEnvironmentVariableA("TJ_D3D8_ARGS", args, sizeof args) > 0 && args[0] == '1';
    if (g_args) arm_args();
    char io[8] = {};
    g_io = GetEnvironmentVariableA("TJ_IO_LOG", io, sizeof io) > 0 && io[0] == '1';
    char fixed[8] = {};
    g_fixed_cursor =
        GetEnvironmentVariableA("TJ_FIXED_CURSOR", fixed, sizeof fixed) > 0 && fixed[0] == '1';
    char background[8] = {};
    g_background = GetEnvironmentVariableA("TJ_BACKGROUND", background, sizeof background) > 0 &&
                   background[0] == '1';
    char self[16] = {};
    g_game_dir_self = GetEnvironmentVariableA("TJ_GAME_DIR", self, sizeof self) > 0 &&
                      !_stricmp(self, "self");
    set_game_dir();
    char activate[8] = {};
    g_activate_trace =
        GetEnvironmentVariableA("TJ_ACTIVATE_TRACE", activate, sizeof activate) > 0 &&
        activate[0] == '1';
    char stall_watch[8] = {};
    if (GetEnvironmentVariableA("TJ_STALL_WATCH", stall_watch, sizeof stall_watch) > 0 &&
        stall_watch[0] == '1') {
        stall::arm();
    }
    char trace[8] = {};
    g_trace = GetEnvironmentVariableA("TJ_CLOCK_TRACE", trace, sizeof trace) > 0 && trace[0] == '1';
    char replay_path[MAX_PATH * 2] = {};
    DWORD replay_len = GetEnvironmentVariableA("TJ_REPLAY", replay_path, sizeof replay_path);
    if (replay_len > 0 && replay_len < sizeof replay_path) {
        g_replay = load_replay(replay_path);
        if (g_log) {
            if (g_replay) {
                std::fprintf(g_log, "# replay script %s events=%u\n", replay_path, g_event_count);
            } else {
                std::fprintf(g_log, "# replay bad script %s\n", replay_path);
            }
            std::fflush(g_log);
        }
    }
}

void read_config() {
    char dll[MAX_PATH] = {};
    GetModuleFileNameA(g_module, dll, MAX_PATH);
    char* slash = std::strrchr(dll, '\\');
    if (slash) *slash = 0;
    char path[MAX_PATH * 2] = {};
    std::snprintf(path, sizeof path, "%s\\..\\port\\proxy\\%s", dll, config::kFileName);
    char text[2048] = {};
    FILE* file = std::fopen(path, "rb");
    if (file) {
        size_t got = std::fread(text, 1, sizeof text - 1, file);
        std::fclose(file);
        text[got] = 0;
    } else {
        size_t length = std::strlen(config::kDefault);
        std::memcpy(text, config::kDefault, length + 1);
        file = std::fopen(path, "wb");
        if (file) {
            std::fwrite(config::kDefault, 1, length, file);
            std::fclose(file);
        }
    }
    g_faithful = config::faithful(text) != 0;
    char forced[8] = {};
    bool overridden = GetEnvironmentVariableA("TJ_FAITHFUL", forced, sizeof forced) > 0;
    if (overridden) g_faithful = forced[0] == '1';
    char mode[24] = {};
    config::value(text, "window=", mode, sizeof mode);
    char scale[8] = {};
    config::value(text, "scale=", scale, sizeof scale);
    g_step_off = config::flag(text, "step=", 1) == 0;
    char trace[8] = {};
    g_render_trace = GetEnvironmentVariableA("TJ_RENDER_TRACE", trace, sizeof trace) > 0 &&
                     trace[0] == '1';

    config::value(text, "render=", g_render_selected, sizeof g_render_selected);
    GetEnvironmentVariableA("TJ_RENDER", g_render_selected, sizeof g_render_selected);
    if (std::strcmp(g_render_selected, "d3d12") != 0) {
        std::snprintf(g_render_selected, sizeof g_render_selected, "legacy");
    }
    char step[8] = {};
    if (GetEnvironmentVariableA("TJ_STEP", step, sizeof step) > 0) g_step_off = step[0] == '0';

    GetEnvironmentVariableA("TJ_WINDOW", mode, sizeof mode);
    GetEnvironmentVariableA("TJ_SCALE", scale, sizeof scale);
    g_window_kind = windowmode::kind(mode);
    std::snprintf(g_scale, sizeof g_scale, "%s", scale[0] ? scale : "1");
    if (g_log) {
        std::fprintf(g_log, "# config %s faithful=%d%s step=%s window=%s scale=%s\n", path,
                     g_faithful ? 1 : 0, overridden ? " (env)" : "", g_step_off ? "game" : "fixed",
                     g_window_kind == windowmode::kBorderless     ? "borderless"
                     : g_window_kind == windowmode::kDesktop      ? "fullscreen-desktop"
                                                                  : "windowed",
                     g_scale);
        std::fflush(g_log);
    }
}

void arm_probe() {
    char text[2048] = {};
    SetLastError(ERROR_SUCCESS);
    DWORD n = GetEnvironmentVariableA("TJ_PROBE", text, sizeof text);
    if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return;
    text[sizeof text - 1] = 0;
    if (!g_replay) {
        if (g_log) {
            std::fprintf(g_log, "# probe refused (replay only)\n");
            std::fflush(g_log);
        }
        return;
    }
    if (n >= sizeof text ||
        !probe::parse(text, g_probe_terms, probe::kMaxTerms, &g_probe_count)) {
        if (g_log) {
            std::fprintf(g_log, "# probe bad list %s\n", text);
            std::fflush(g_log);
        }
        return;
    }
    g_probe_base = (uintptr_t)GetModuleHandleA(nullptr);
    g_probe = true;
    if (g_log) {
        std::fprintf(g_log, "# probe armed n=%u base=%08X\n", g_probe_count,
                     (unsigned)g_probe_base);
        std::fflush(g_log);
    }
}

void arm_watch() {
    char text[256] = {};
    SetLastError(ERROR_SUCCESS);
    DWORD n = GetEnvironmentVariableA("TJ_WATCH", text, sizeof text);
    if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return;
    text[sizeof text - 1] = 0;
    if (!g_replay) {
        if (g_log) {
            std::fprintf(g_log, "# watch refused (replay only)\n");
            std::fflush(g_log);
        }
        return;
    }
    if (n >= sizeof text || !watch::parse(text, &g_watch_term)) {
        if (g_log) {
            std::fprintf(g_log, "# watch bad term %s\n", text);
            std::fflush(g_log);
        }
        return;
    }
    g_watch = true;
    g_watch_base = (uintptr_t)GetModuleHandleA(nullptr);
    if (g_log) {
        std::fprintf(g_log, "# watch named %s\n", g_watch_term.text);
        std::fflush(g_log);
    }
}

void arm_census() {
    char text[32] = {};
    SetLastError(ERROR_SUCCESS);
    DWORD n = GetEnvironmentVariableA("TJ_CENSUS", text, sizeof text);
    if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return;
    if (!g_replay || n >= sizeof text || !std::strtoul(text, nullptr, 10)) {
        if (g_log) {
            std::fprintf(g_log, "# census refused (replay only, frame number needed)\n");
            std::fflush(g_log);
        }
        return;
    }
    g_census_end = (unsigned)std::strtoul(text, nullptr, 10);
    g_census = true;
    if (g_log) {
        std::fprintf(g_log, "# census armed end=%u step=%u\n", g_census_end, kCensusStep);
        std::fflush(g_log);
    }
}

void arm_snapshot() {
    char text[32] = {};
    SetLastError(ERROR_SUCCESS);
    DWORD n = GetEnvironmentVariableA("TJ_SNAPSHOT", text, sizeof text);
    if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return;
    if (!g_replay || g_faithful || n >= sizeof text || !std::strtoul(text, nullptr, 10)) {
        if (g_log) {
            std::fprintf(g_log, "# snapshot refused (replay, faithful=0 and a frame number)\n");
            std::fflush(g_log);
        }
        return;
    }
    g_snapshot_at = (unsigned)std::strtoul(text, nullptr, 10);
    g_snapshot = true;
    if (g_log) {
        std::fprintf(g_log, "# snapshot armed at=%u verify=%u restore=%u\n", g_snapshot_at,
                     g_snapshot_at + kSnapshotGap, g_snapshot_at + 2 * kSnapshotGap);
        std::fflush(g_log);
    }
}

}

extern "C" int StepDecide(float delta) {
    if (g_log && g_step_trace < kStepTrace) {
        ++g_step_trace;
        uint32_t bits = 0;
        std::memcpy(&bits, &delta, 4);
        EnterCriticalSection(&g_lock);
        std::fprintf(g_log, "# step delta %08X call=%u\n", bits, g_step_trace);
        std::fflush(g_log);
        LeaveCriticalSection(&g_lock);
    }
    int steps;
    if (g_replay) {
        steps = 1;
    } else {
        LARGE_INTEGER now = {};
        QueryPerformanceCounter(&now);
        fixedstep::add(&g_step_clock, (double)(now.QuadPart - g_step_last.QuadPart) /
                                          (double)g_step_freq.QuadPart);
        g_step_last = now;
        steps = fixedstep::take(&g_step_clock);
    }
    g_step_count += (unsigned)steps;
    return steps;
}

// The step driver's ABI: ecx (its this) in, the delta on the stack, ret 4 out; esi and edi survive the driver's own calls.
extern "C" __declspec(naked) void StepHook(void) {
    __asm {
        push esi
        push edi
        mov esi, ecx
        push dword ptr [esp + 12]
        call StepDecide
        add esp, 4
        mov edi, eax
        test edi, edi
        jle done
    again:
        mov ecx, esi
        push dword ptr [g_step_delta]
        call dword ptr [g_step_entry]
        dec edi
        jnz again
    done:
        pop edi
        pop esi
        ret 4
    }
}

const unsigned kD3d12Caps[52] = {
    0x00000001, 0x00000000, 0x00020000, 0xE00A0000, 0x000003A0, 0x8000000F, 0x00000001,
    0x001BBEF0, 0x002FCEF2, 0x07736191, 0x000000FF, 0x00003FFF, 0x00003FFF, 0x000000FF,
    0x00084208, 0x0001ECC5, 0x03030700, 0x03030300, 0x03030300, 0x0000003F, 0x0000003F,
    0x0000001F, 0x00004000, 0x00004000, 0x00000800, 0x00002000, 0x00004000, 0x00000010,
    0x501502F9, 0xCCBEBC20, 0xCCBEBC20, 0x4CBEBC20, 0x4CBEBC20, 0x00000000, 0x000000FF,
    0x00180008, 0x03FEFFFF, 0x00000008, 0x00000008, 0x0000017B, 0x0000000A, 0x00000008,
    0x00000004, 0x00000008, 0x46000000, 0x00FFFFFF, 0x00FFFFFF, 0x00000010, 0x000000FF,
    0xFFFE0101, 0x00000100, 0xFFFF0104,
};

struct FakeRoot {
    void** vtbl;
    ULONG refs;
};

void fake_format(const char* what, DWORD a, DWORD b, DWORD c, DWORD d) {
    if (!g_log || !g_render_trace) return;
    std::fprintf(g_log, "# d3d8 %s a=%08X b=%08X c=%08X d=%08X\n", what, (unsigned)a, (unsigned)b,
                 (unsigned)c, (unsigned)d);
    std::fflush(g_log);
}

void fake_note(const char* what) {
    if (!g_log) return;
    std::fprintf(g_log, "# d3d8 %s\n", what);
    std::fflush(g_log);
}

ULONG __stdcall F_AddRef(void* self) { return ++static_cast<FakeRoot*>(self)->refs; }
ULONG __stdcall F_Release(void* self) {
    FakeRoot* root = static_cast<FakeRoot*>(self);
    ULONG left = --root->refs;
    if (!left) delete root;
    return left;
}
HRESULT __stdcall F_QueryInterface(void* self, const void* iid, void** out) {
    (void)iid;
    F_AddRef(self);
    if (out) *out = self;
    return S_OK;
}
HRESULT __stdcall F_NotImplemented(void* self) {
    (void)self;
    return E_NOTIMPL;
}
UINT __stdcall F_GetAdapterCount(void* self) {
    (void)self;
    fake_note("GetAdapterCount");
    unsigned count = 1;
    query_line("GetAdapterCount", 0, 0, 0, 0, S_OK, &count, 1);
    return 1;
}
HRESULT __stdcall F_GetAdapterIdentifier(void* self, UINT adapter, DWORD flags, void* out) {
    (void)self;
    fake_note("GetAdapterIdentifier");
    if (!out) {
        query_line("GetAdapterIdentifier", adapter, flags, 0, 0, E_POINTER, nullptr, 0);
        return E_POINTER;
    }
    unsigned char* p = static_cast<unsigned char*>(out);
    std::memset(p, 0, 1068);
    std::snprintf((char*)p + 512, 512, "TJ D3D12");
    p[1024] = 1;
    unsigned values[5] = {};
    for (unsigned i = 0; i < 4; ++i) values[i] = ((const unsigned*)out)[258 + i];
    values[4] = ((const unsigned*)out)[1064 / 4];
    query_line("GetAdapterIdentifier", adapter, flags, 0, 0, S_OK, values, 5);
    return S_OK;
}

struct Mode {
    unsigned width, height;
};
const Mode kModes[] = {{600, 450},  {640, 480},  {800, 600},  {1024, 768}, {1152, 864},
                       {1280, 720}, {1280, 960}, {1280, 1024}, {1440, 900}, {1600, 1200},
                       {1920, 1080}, {1920, 1200}};
UINT __stdcall F_GetAdapterModeCount(void* self, UINT adapter) {
    (void)self;
    (void)adapter;
    fake_note("GetAdapterModeCount");
    unsigned count = sizeof kModes / sizeof kModes[0];
    query_line("GetAdapterModeCount", adapter, 0, 0, 0, S_OK, &count, 1);
    return sizeof kModes / sizeof kModes[0];
}
HRESULT __stdcall F_EnumAdapterModes(void* self, UINT adapter, UINT mode, void* out) {
    (void)self;
    fake_note("EnumAdapterModes");
    if (!out || mode >= sizeof kModes / sizeof kModes[0]) {
        query_line("EnumAdapterModes", adapter, mode, 0, 0, E_INVALIDARG, nullptr, 0);
        return E_INVALIDARG;
    }
    unsigned* m = static_cast<unsigned*>(out);
    m[0] = kModes[mode].width;
    m[1] = kModes[mode].height;
    m[2] = 60;
    m[3] = render::kFmtX8R8G8B8;
    query_line("EnumAdapterModes", adapter, mode, 0, 0, S_OK, m, 4);
    return S_OK;
}
HRESULT __stdcall F_GetAdapterDisplayMode(void* self, UINT adapter, void* out) {
    (void)self;
    (void)adapter;
    fake_note("GetAdapterDisplayMode");
    if (!out) {
        query_line("GetAdapterDisplayMode", adapter, 0, 0, 0, E_POINTER, nullptr, 0);
        return E_POINTER;
    }
    unsigned* m = static_cast<unsigned*>(out);
    m[0] = 600;
    m[1] = 450;
    m[2] = 60;
    m[3] = render::kFmtX8R8G8B8;
    query_line("GetAdapterDisplayMode", adapter, 0, 0, 0, S_OK, m, 4);
    return S_OK;
}

constexpr HRESULT kNotAvailable = (HRESULT)0x8876086A;

bool format_ok(DWORD format, DWORD usage) {
    switch (format) {
        case render::kFmtA8R8G8B8:
        case render::kFmtX8R8G8B8: return true;
        case render::kFmtDXT1:
        case render::kFmtDXT3:
        case render::kFmtDXT5: return (usage & 0x3) == 0;
        case render::kFmtD16:
        case render::kFmtD24X8:
        case render::kFmtD24S8:
        case render::kFmtD32: return (usage & 0x3) != 0;
        case 23:
        case 24:
        case 25:
        case 26:
            return true;
        default: return false;
    }
}
HRESULT __stdcall F_CheckDeviceType(void* self, UINT adapter, unsigned type, DWORD display,
                                    DWORD back, BOOL windowed) {
    fake_format("CheckDeviceType", display, back, 0, windowed);
    (void)self;
    (void)windowed;
    HRESULT hr = (display == render::kFmtX8R8G8B8 || display == render::kFmtA8R8G8B8) &&
                         (back == render::kFmtX8R8G8B8 || back == render::kFmtA8R8G8B8)
                     ? S_OK
                     : kNotAvailable;
    unsigned extra[2] = {adapter, type};
    query_line("CheckDeviceType", display, back, windowed, type, hr, extra, 2);
    return hr;
}
HRESULT __stdcall F_CheckDeviceFormat(void* self, UINT adapter, unsigned type, DWORD adapterFormat,
                                      DWORD usage, unsigned rtype, DWORD checkFormat) {
    fake_format("CheckDeviceFormat", usage, checkFormat, rtype, 0);
    (void)self;

    HRESULT hr = rtype == 4 ? kNotAvailable
                            : (adapterFormat == checkFormat && adapterFormat == render::kFmtA8R8G8B8)
                                  ? kNotAvailable
                              : (adapterFormat == checkFormat && (adapterFormat == 25 || adapterFormat == 26))
                                    ? kNotAvailable
                                : format_ok(checkFormat, usage) ? S_OK : kNotAvailable;
    if (FAILED(hr) && g_log) {
        std::fprintf(g_log, "# d3d8 FAIL CheckDeviceFormat usage=%08X rtype=%u format=%08X\n",
                     (unsigned)usage, rtype, (unsigned)checkFormat);
        std::fflush(g_log);
    }
    unsigned extra[2] = {adapter, type};
    query_line("CheckDeviceFormat", adapterFormat, usage, rtype, checkFormat, hr, extra, 2);
    return hr;
}
HRESULT __stdcall F_CheckDeviceMultiSampleType(void* self, UINT adapter, unsigned type,
                                               DWORD surfaceFormat, BOOL windowed,
                                               unsigned multisample) {
    fake_format("CheckDeviceMultiSampleType", surfaceFormat, 0, 0, multisample);
    (void)self;
    (void)windowed;
    HRESULT hr = multisample == 0 ? S_OK : kNotAvailable;
    unsigned extra[2] = {adapter, type};
    query_line("CheckDeviceMultiSampleType", surfaceFormat, windowed, multisample, 0, hr, extra, 2);
    return hr;
}
HRESULT __stdcall F_CheckDepthStencilMatch(void* self, UINT adapter, unsigned type,
                                           DWORD adapterFormat, DWORD renderTargetFormat,
                                           DWORD depthFormat) {
    fake_format("CheckDepthStencilMatch", renderTargetFormat, depthFormat, 0, 0);
    (void)self;
    (void)adapterFormat;
    (void)renderTargetFormat;
    HRESULT hr = format_ok(depthFormat, 2) ? S_OK : kNotAvailable;
    unsigned extra[2] = {adapter, type};
    query_line("CheckDepthStencilMatch", adapterFormat, renderTargetFormat, depthFormat, 0, hr,
               extra, 2);
    return hr;
}
HRESULT __stdcall F_GetDeviceCaps(void* self, UINT adapter, unsigned type, void* caps) {
    (void)self;
    (void)adapter;
    (void)type;
    fake_note("GetDeviceCaps");
    if (!caps) {
        query_line("GetDeviceCaps", adapter, type, 0, 0, E_POINTER, nullptr, 0);
        return E_POINTER;
    }
    std::memcpy(caps, kD3d12Caps, sizeof kD3d12Caps);
    query_line("GetDeviceCaps", adapter, type, 0, 0, S_OK, (const unsigned*)caps, 52);
    return S_OK;
}
HRESULT __stdcall F_CreateDevice(void* self, unsigned adapter, unsigned type, HWND focus,
                                 DWORD flags, void* params, void** out);
void* fake_device_table();

void* fake_root() {
    static void* vtbl[16] = {
        (void*)&F_QueryInterface,      (void*)&F_AddRef,
        (void*)&F_Release,             (void*)&F_NotImplemented,
        (void*)&F_GetAdapterCount,     (void*)&F_GetAdapterIdentifier,
        (void*)&F_GetAdapterModeCount, (void*)&F_EnumAdapterModes,
        (void*)&F_GetAdapterDisplayMode, (void*)&F_CheckDeviceType,
        (void*)&F_CheckDeviceFormat,   (void*)&F_CheckDeviceMultiSampleType,
        (void*)&F_CheckDepthStencilMatch, (void*)&F_GetDeviceCaps,
        (void*)&F_NotImplemented,
        (void*)&F_CreateDevice,
    };
    FakeRoot* root = new FakeRoot;
    root->vtbl = vtbl;
    root->refs = 1;
    render::root_com_slot() = root;
    return root;
}

HRESULT __stdcall A_Unimplemented(void* self) {
    (void)self;
    return E_NOTIMPL;
}

void* fake_device_table() {
    static void** table = nullptr;
    if (table) return table;
    table = new void*[SLOT_COUNTS[1]];
    for (unsigned i = 0; i < SLOT_COUNTS[1]; ++i) table[i] = (void*)&A_Unimplemented;
    for (const SlotAdapter& adapter : kAdapters) table[adapter.slot] = adapter.function;
    table[kPresent] = (void*)&ProxyPresent;
    return table;
}

HRESULT __stdcall F_CreateDevice(void* self, unsigned adapter, unsigned type, HWND focus,
                                 DWORD flags, void* params, void** out) {
    (void)self;
    fake_note("CreateDevice");
    (void)adapter;
    (void)type;
    (void)flags;
    if (!out) return E_POINTER;
    unsigned w = param32(params, 0);
    unsigned h = param32(params, 4);
    DWORD format = param32(params, 8);
    g_hwnd = focus;
    g_back_w = w;
    g_back_h = h;
    render::Device* backend = render::create_d3d12(focus, w, h, format, flags, kD3d12Caps);
    if (!backend) {
        if (g_log) {
            std::fprintf(g_log, "# d3d12 device creation failed step=%u\n",
                         render::d3d12_fail_step());
            std::fflush(g_log);
        }
        return E_FAIL;
    }
    apply_window_plan(focus, params);
    Wrapper* wrapper = new Wrapper;
    wrapper->real = nullptr;
    wrapper->vtbl = (void**)fake_device_table();
    g_device_com = wrapper;
    render::device_com_slot() = wrapper;
    if (g_capture) arm_capture(wrapper, params, focus);
    if (g_snapshot && !g_capture) g_device = wrapper;
    if (g_args) record_present_params(params);
    *out = wrapper;
    log_create_device("d3d12", adapter, type, flags, params, S_OK);
    log_render_surfaces("d3d12", backend);
    if (g_log) {
        std::fprintf(g_log, "# render backend=d3d12 device width=%u height=%u format=%u\n", w, h,
                     format);
        std::fflush(g_log);
    }
    return S_OK;
}

extern "C" void* __stdcall Direct3DCreate8(unsigned version) {

    if (g_io || g_replay || g_fixed_cursor || g_background || g_activate_trace) arm_io();
    install_step_hook();
    if (std::strcmp(g_render_selected, "d3d12") == 0) {

        if (g_silence && !g_silence_started) {
            g_silence_started = true;
            CreateThread(nullptr, 0, close_script_errors, nullptr, 0, nullptr);
        }
        if (g_background && !g_focus_guard) {
            g_focus_guard = true;
            CreateThread(nullptr, 0, focus_guard_thread, nullptr, 0, nullptr);
        }
        if (g_log) {
            std::fprintf(g_log, "# render backend=d3d12 no provider\n");
            std::fflush(g_log);
        }
        return fake_root();
    }
    HMODULE module = real_dll();
    if (!module) return nullptr;
    typedef void*(__stdcall * Fn)(unsigned);
    auto create = (Fn)GetProcAddress(module, "Direct3DCreate8");
    if (!create) {
        if (g_log) {
            std::fprintf(g_log, "# no Direct3DCreate8 in the system dll\n");
            std::fflush(g_log);
        }
        return nullptr;
    }
    void* real = create(version);
    if (g_background && !g_focus_guard) {

        g_focus_guard = true;
        CreateThread(nullptr, 0, focus_guard_thread, nullptr, 0, nullptr);
    }
    if (g_silence && !g_silence_started) {
        g_silence_started = true;
        CreateThread(nullptr, 0, close_script_errors, nullptr, 0, nullptr);
        if (g_log) {
            std::fprintf(g_log, "# silence armed\n");
            std::fflush(g_log);
        }
    }
    void* root = real ? wrap(real, 0) : nullptr;
    render::root_com_slot() = root;
    return root;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);

        if (std::strcmp(SLOT_NAMES[0][kCreateDevice], "CreateDevice") != 0) return FALSE;
        InitializeCriticalSection(&g_lock);
        open_log();
        read_capture_env();
        read_config();
        char skipDraw[32] = {};
        if (GetEnvironmentVariableA("TJ_SKIP_DRAW", skipDraw, sizeof skipDraw) > 0) {
            unsigned frame = 0;
            unsigned first = 0;
            unsigned last = 0;
            int fields = std::sscanf(skipDraw, "%u:%u-%u", &frame, &first, &last);
            if (fields < 2) fields = std::sscanf(skipDraw, "%u:%u", &frame, &first);
            if (fields >= 2) {
                render::skip_frame_slot() = frame;
                render::skip_index_slot() = first;
                render::skip_end_slot() = fields >= 3 ? last : first;
            }
        }
        char onlyDraw[32] = {};
        if (GetEnvironmentVariableA("TJ_ONLY_DRAW", onlyDraw, sizeof onlyDraw) > 0) {
            unsigned frame = 0;
            unsigned index = 0;
            if (std::sscanf(onlyDraw, "%u:%u", &frame, &index) == 2) {
                render::only_frame_slot() = frame;
                render::only_index_slot() = index;
            }
        }
        char drawState[32] = {};
        if (GetEnvironmentVariableA("TJ_DRAW_STATE", drawState, sizeof drawState) > 0) {
            unsigned frame = 0;
            unsigned first = 0;
            unsigned last = 0;
            int fields = std::sscanf(drawState, "%u:%u-%u", &frame, &first, &last);
            if (fields < 2) fields = std::sscanf(drawState, "%u:%u", &frame, &first);
            if (fields >= 2) {
                g_draw_state = true;
                g_draw_state_frame = frame;
                g_draw_state_first = first;
                g_draw_state_last = fields >= 3 ? last : first;
            }
        }
        char inputDump[32] = {};
        if (GetEnvironmentVariableA("TJ_INPUT_DUMP", inputDump, sizeof inputDump) > 0) {
            unsigned frame = 0;
            unsigned first = 0;
            unsigned last = 0;
            int fields = std::sscanf(inputDump, "%u:%u-%u", &frame, &first, &last);
            if (fields < 2) fields = std::sscanf(inputDump, "%u:%u", &frame, &first);
            if (fields >= 2) {
                g_input_dump = true;
                g_input_dump_frame = frame;
                g_input_dump_first = first;
                g_input_dump_last = fields >= 3 ? last : first;
            }
        }
        char texHistory[32] = {};
        if (GetEnvironmentVariableA("TJ_TEX_HISTORY", texHistory, sizeof texHistory) > 0) {
            g_tex_history_frame = (unsigned)std::strtoul(texHistory, nullptr, 10);
            g_tex_history = g_tex_history_frame > 0;
        }
        char texBytes[32] = {};
        if (GetEnvironmentVariableA("TJ_TEX_BYTES", texBytes, sizeof texBytes) > 0) {
            g_tex_bytes_frame = (unsigned)std::strtoul(texBytes, nullptr, 10);
        }
        if (g_io || g_replay || g_fixed_cursor || g_background || g_activate_trace ||
            g_game_dir_self) arm_io();

        AddVectoredExceptionHandler(1, trace_exception);
        if (g_activate_trace) {
            if (CreateThread(nullptr, 0, trace_foreground_thread, nullptr, 0, nullptr) && g_log) {
                std::fprintf(g_log, "# trace armed\n");
                std::fflush(g_log);
            }
            g_api.exitProcess =
                (ExitProcessFn)patch_or_log("KERNEL32.dll", "ExitProcess", (void*)&IoExitProcess);
            g_api.terminateProcess = (TerminateProcessFn)patch_or_log(
                "KERNEL32.dll", "TerminateProcess", (void*)&IoTerminateProcess);
        }
        if ((g_replay || g_fixed_cursor) && g_log) {
            std::fprintf(g_log, "# cursor fixed x=%ld y=%ld\n", replay::kCursorX,
                         replay::kCursorY);

            std::fprintf(g_log, "# tid main=%u\n", GetCurrentThreadId());
            std::fflush(g_log);
        }
        if (g_replay) {

            replay::clock_start(replay::kSeed);
            replay::clock_filetime_base() = replay::kFiletimeBase;
            if (g_log) {
                std::fprintf(g_log, "# replay clock start seed=%lld filetime=%lld\n",
                             (long long)replay::kSeed, (long long)replay::kFiletimeBase);
                std::fflush(g_log);
            }
        }
        arm_probe();
        arm_watch();
        install_arm_hook();
        arm_census();
        arm_snapshot();
        arm_save();
        arm_load();
        if (g_capture) {
            bool named = !std::strcmp(SLOT_NAMES[1][kPresent], "Present") &&
                         !std::strcmp(SLOT_NAMES[1][kBackBuffer], "GetBackBuffer") &&
                         !std::strcmp(SLOT_NAMES[1][kCreateImageSurface], "CreateImageSurface") &&
                         !std::strcmp(SLOT_NAMES[1][kCopyRects], "CopyRects");
            g_capture = named;
            if (g_log) {
                if (named) {
                    std::fprintf(g_log, "# capture armed at=%u out=%s\n", g_capture_at,
                                 g_capture_out);
                } else {
                    std::fprintf(g_log, "# capture slot names disagree with the pinned header\n");
                }
                std::fflush(g_log);
            }
        }
        if (g_args && g_log) {
            std::fprintf(g_log, "# args armed\n");
            std::fflush(g_log);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_log) {
            if (TryEnterCriticalSection(&g_lock)) {
                flush_locked(GetTickCount());
                if (g_args) flush_args_locked();
                LeaveCriticalSection(&g_lock);
            }
            std::fclose(g_log);
            g_log = nullptr;
        }
        DeleteCriticalSection(&g_lock);
    }
    return TRUE;
}
