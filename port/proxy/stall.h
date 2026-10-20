
#pragma once

#include <atomic>

namespace stall {

struct Sample {
    unsigned index = 0, seconds = 0;
    DWORD tid = 0, openError = 0, suspendError = 0, suspendPrevious = 0, contextError = 0;
    bool opened = false, suspended = false, gotContext = false;
    unsigned resumeError = 0;
    CONTEXT context = {};
    uintptr_t frames[16] = {};
    unsigned frameCount = 0;
    uintptr_t stackAt[24] = {};
    uintptr_t stackValue[24] = {};
    unsigned stackCount = 0;
};

inline std::atomic<LONG64>& last_tick() {
    static std::atomic<LONG64> tick{0};
    return tick;
}
inline std::atomic<DWORD>& presenting_thread() {
    static std::atomic<DWORD> tid{0};
    return tid;
}
inline std::atomic<int>& start_state() {
    static std::atomic<int> state{0};
    return state;
}
inline std::atomic<unsigned>& gap_ms() {
    static std::atomic<unsigned> ms{3000};
    return ms;
}
inline std::atomic<bool>& armed() {
    static std::atomic<bool> on{false};
    return on;
}
inline std::atomic<uintptr_t>& exe_base() {
    static std::atomic<uintptr_t> base{0};
    return base;
}
inline std::atomic<size_t>& exe_size() {
    static std::atomic<size_t> size{0};
    return size;
}
inline std::atomic<uintptr_t>& dll_base() {
    static std::atomic<uintptr_t> base{0};
    return base;
}
inline std::atomic<size_t>& dll_size() {
    static std::atomic<size_t> size{0};
    return size;
}
inline Sample& sample_slot() {
    static Sample sample;
    return sample;
}

inline void touch() { last_tick().store((LONG64)GetTickCount64(), std::memory_order_release); }

inline size_t module_size(HMODULE module) {
    if (!module) return 0;
    const unsigned char* base = (const unsigned char*)module;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

struct Module {
    const char* name;
    uintptr_t base;
    size_t size;
};
inline Module* modules() {
    static Module list[12] = {};
    return list;
}
inline void resolve_modules() {
    static const char* const kNames[] = {"ntdll.dll",     "kernel32.dll",  "kernelbase.dll",
                                         "user32.dll",    "gdi32.dll",     "msvcrt.dll",
                                         "combase.dll",   "ole32.dll",     "d3d12.dll",
                                         "dxgi.dll",      "winmm.dll",     "dinput8.dll"};
    Module* list = modules();
    for (unsigned i = 0; i < sizeof kNames / sizeof kNames[0]; ++i) {
        HMODULE module = GetModuleHandleA(kNames[i]);
        list[i].name = kNames[i];
        list[i].base = (uintptr_t)module;
        list[i].size = module_size(module);
    }
}

inline const char* where(uintptr_t address) {
    if (address >= exe_base().load() && address < exe_base().load() + exe_size().load()) return "exe";
    if (address >= dll_base().load() && address < dll_base().load() + dll_size().load())
        return "d3d8";
    Module* list = modules();
    for (unsigned i = 0; i < 12; ++i) {
        if (list[i].base && address >= list[i].base && address < list[i].base + list[i].size)
            return list[i].name;
    }
    return "lib";
}

inline uintptr_t rva(uintptr_t address) {
    if (address >= exe_base().load() && address < exe_base().load() + exe_size().load())
        return address - exe_base().load();
    if (address >= dll_base().load() && address < dll_base().load() + dll_size().load())
        return address - dll_base().load();
    return address;
}

inline bool readable(uintptr_t address) {
    SIZE_T got = 0;
    DWORD value = 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void*)address, &value, sizeof value, &got) &&
           got == sizeof value;
}

inline void capture(unsigned index, unsigned seconds) {
    Sample& s = sample_slot();
    s = Sample{};
    s.index = index;
    s.seconds = seconds;
    DWORD tid = presenting_thread().load(std::memory_order_acquire);
    s.tid = tid;
    if (!tid) return;
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, tid);
    if (!thread) {
        s.openError = GetLastError();
        return;
    }
    s.opened = true;
    DWORD previous = SuspendThread(thread);
    if (previous == (DWORD)-1) {

        s.suspendError = GetLastError();
        CloseHandle(thread);
        return;
    }
    s.suspended = true;
    s.suspendPrevious = previous;
    CONTEXT context = {};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (GetThreadContext(thread, &context)) {
        s.gotContext = true;
        s.context = context;
        uintptr_t frame = context.Ebp;
        for (unsigned depth = 0; depth < 16 && frame && s.frameCount < 16; ++depth) {
            uintptr_t next = 0;
            uintptr_t back = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), (const void*)frame, &next, sizeof next,
                                   nullptr) ||
                !ReadProcessMemory(GetCurrentProcess(), (const void*)(frame + 4), &back, sizeof back,
                                   nullptr)) {
                break;
            }
            s.frames[s.frameCount++] = back;
            if (next <= frame) break;
            frame = next;
        }
        const uintptr_t base = exe_base().load();
        const size_t size = exe_size().load();
        for (uintptr_t at = context.Esp; at < context.Esp + 0x2000 && s.stackCount < 24; at += 4) {
            if (!readable(at)) break;
            uintptr_t value = *(const uintptr_t*)at;
            if (value < base || value >= base + size) continue;
            s.stackAt[s.stackCount] = at;
            s.stackValue[s.stackCount] = value;
            ++s.stackCount;
        }
    } else {
        s.contextError = GetLastError();
    }

    if (ResumeThread(thread) == (DWORD)-1) s.resumeError = GetLastError();
    CloseHandle(thread);
}

inline void log_sample() {
    const Sample& s = sample_slot();
    if (!s.suspended) {
        if (!s.tid) {
            render::log_line("# stall sample=%u no presenting thread recorded\n", s.index);
        } else if (!s.opened) {
            render::log_line("# stall sample=%u open thread %u failed err=%u\n", s.index, s.tid,
                             (unsigned)s.openError);
        } else {
            render::log_line("# stall sample=%u suspend thread %u failed err=%u\n", s.index, s.tid,
                             (unsigned)s.suspendError);
        }
        return;
    }
    const CONTEXT& c = s.context;
    if (s.gotContext) {
        render::log_line("# stall sample=%u seconds=%u tid=%u eip=%08X %s rva=%08X esp=%08X "
                         "ebp=%08X eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X\n",
                         s.index, s.seconds, s.tid, (unsigned)c.Eip, where(c.Eip),
                         (unsigned)rva(c.Eip), (unsigned)c.Esp, (unsigned)c.Ebp, (unsigned)c.Eax,
                         (unsigned)c.Ebx, (unsigned)c.Ecx, (unsigned)c.Edx, (unsigned)c.Esi,
                         (unsigned)c.Edi);
        for (unsigned i = 0; i < s.frameCount; ++i) {
            render::log_line("# stall frame=%u at=%08X %s rva=%08X\n", i, (unsigned)s.frames[i],
                             where(s.frames[i]), (unsigned)rva(s.frames[i]));
        }
        for (unsigned i = 0; i < s.stackCount; ++i) {
            render::log_line("# stall stack=%08X exe rva=%08X\n", (unsigned)s.stackAt[i],
                             (unsigned)rva(s.stackValue[i]));
        }
    } else {
        render::log_line("# stall sample=%u get context failed err=%u\n", s.index,
                         (unsigned)s.contextError);
    }
    if (s.resumeError) {
        render::log_line("# stall sample=%u resume failed err=%u\n", s.index, (unsigned)s.resumeError);
    }
}

inline DWORD WINAPI watchdog(LPVOID) {
    unsigned samples = 0;
    for (;;) {
        Sleep(500);
        LONG64 last = last_tick().load(std::memory_order_acquire);
        if (!last) continue;
        unsigned long long gap = (unsigned long long)((LONG64)GetTickCount64() - last);
        if (gap < (unsigned long long)gap_ms().load(std::memory_order_relaxed)) {
            samples = 0;
            continue;
        }
        if (samples >= 5) continue;
        ++samples;
        unsigned silent = (unsigned)(gap / 1000);
        render::log_line("# stall no-present seconds=%u sample=%u\n", silent, samples);
        capture(samples, silent);
        log_sample();
    }
    return 0;
}

inline void present() {
    if (!armed().load(std::memory_order_acquire)) return;
    presenting_thread().store(GetCurrentThreadId(), std::memory_order_release);
    touch();
    int state = start_state().load(std::memory_order_acquire);
    if (state) return;
    int expected = 0;
    if (!start_state().compare_exchange_strong(expected, 1)) return;
    HANDLE thread = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
        start_state().store(2, std::memory_order_release);
    } else {
        start_state().store(-1, std::memory_order_release);
        render::log_line("# stall watchdog thread failed err=%u\n", (unsigned)GetLastError());
    }
}

inline void arm() {
    exe_base().store((uintptr_t)GetModuleHandleA(nullptr));
    exe_size().store(module_size(GetModuleHandleA(nullptr)));
    dll_base().store((uintptr_t)GetModuleHandleA("d3d8.dll"));
    dll_size().store(module_size((HMODULE)dll_base().load()));
    resolve_modules();
    char seconds[16] = {};
    if (GetEnvironmentVariableA("TJ_STALL_SECONDS", seconds, sizeof seconds) > 0) {
        unsigned value = (unsigned)std::strtoul(seconds, nullptr, 10);
        if (value >= 1 && value <= 120) gap_ms().store(value * 1000);
    }
    armed().store(true, std::memory_order_release);
    touch();
    render::log_line("# stall watchdog armed gap_ms=%u exe=%08X+%X dll=%08X+%X\n", gap_ms().load(),
                     (unsigned)exe_base().load(), (unsigned)exe_size().load(),
                     (unsigned)dll_base().load(), (unsigned)dll_size().load());
}

}
