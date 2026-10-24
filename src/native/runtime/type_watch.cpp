// SPDX-License-Identifier: GPL-3.0-only
#include "type_watch.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace btd5loader::runtime {
namespace {

constexpr std::uintptr_t kTypeOffset = 0xD0;
// L0 and L1 enabled; both slots break on read or write (RW=11) of 4 bytes (LEN=11).
constexpr DWORD kArmedDr7 = 0x00FF0005;
// Adds L2 and L3 with the same read/write, 4-byte conditions.
constexpr DWORD kCopyDr7 = 0xFF000050;
// 0x68EA60 copies the tower type into the popped bloon at +0x250; the trap
// after its +0xD0 read (0x68EA89) lands here with the bloon still in EDX.
constexpr std::uintptr_t kCopySiteAfter = 0x68EA8F - 0x400000;
constexpr std::uintptr_t kCopyOffset = 0x250;
constexpr std::size_t kSlotCount = 4096;
constexpr std::size_t kMaxThreads = 256;

struct Slot {
    volatile LONG64 key;  // (after << 32) | frame; 0 means empty
    volatile LONG hits;
    // bits 0/1 = tower +0xD0/+0xD4; bits 2/3 = latest popped bloon's +0x250/+0x254 copy
    volatile LONG half_mask;
};

std::array<Slot, kSlotCount> g_slots{};
volatile LONG g_armed = 0;
volatile LONG g_overflow = 0;
std::uintptr_t g_watched = 0;
std::uintptr_t g_copy_site = 0;
PVOID g_handler = nullptr;

void record(const std::uint32_t after, const std::uint32_t frame, const LONG half) noexcept {
    const LONG64 key = (static_cast<LONG64>(after) << 32) | frame;
    std::size_t index = (static_cast<std::size_t>(after) * 2654435761u ^ frame) % kSlotCount;
    for (std::size_t probe = 0; probe < kSlotCount; ++probe, index = (index + 1) % kSlotCount) {
        Slot& slot = g_slots[index];
        LONG64 current = slot.key;
        if (current == 0) {
            current = InterlockedCompareExchange64(&slot.key, key, 0);
            if (current == 0) current = key;
        }
        if (current == key) {
            InterlockedIncrement(&slot.hits);
            InterlockedOr(&slot.half_mask, half);
            return;
        }
    }
    InterlockedIncrement(&g_overflow);
}

// Reads the return address one EBP frame up; kept free of C++ objects for SEH.
std::uint32_t frame_return(const DWORD ebp) noexcept {
    __try {
        return *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(ebp) + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* const info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    CONTEXT* const context = info->ContextRecord;
    const LONG half = static_cast<LONG>(context->Dr6 & 0xF);
    if (half == 0 || g_armed == 0) return EXCEPTION_CONTINUE_SEARCH;
    const auto after = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    record(static_cast<std::uint32_t>(after), frame_return(context->Ebp), half);
    // Follow the copy: watch the bloon that is about to receive the tower type.
    if (after == g_copy_site && context->Edx != 0) {
        context->Dr2 = context->Edx + kCopyOffset;
        context->Dr3 = context->Edx + kCopyOffset + 4;
        context->Dr7 |= kCopyDr7;
    }
    context->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Sets the debug registers of every other thread in the process from a helper
// thread, because a thread cannot reliably change its own context.
std::size_t apply_debug_registers(const std::uintptr_t address, const DWORD dr7) {
    std::array<DWORD, kMaxThreads> ids{};
    std::size_t count = 0;
    std::size_t applied = 0;
    std::thread worker([&] {
        const DWORD process = GetCurrentProcessId();
        const DWORD self = GetCurrentThreadId();
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Thread32First(snapshot, &entry); more && count < ids.size();
             more = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID == process && entry.th32ThreadID != self) {
                ids[count++] = entry.th32ThreadID;
            }
        }
        CloseHandle(snapshot);
        for (std::size_t i = 0; i < count; ++i) {
            const HANDLE thread = OpenThread(
                THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, ids[i]);
            if (thread == nullptr) continue;
            if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
                CONTEXT context{};
                context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(thread, &context) != FALSE) {
                    context.Dr0 = static_cast<DWORD>(address);
                    context.Dr1 = static_cast<DWORD>(address + 4);
                    context.Dr2 = 0;
                    context.Dr3 = 0;
                    context.Dr6 = 0;
                    context.Dr7 = dr7;
                    if (SetThreadContext(thread, &context) != FALSE) ++applied;
                }
                ResumeThread(thread);
            }
            CloseHandle(thread);
        }
    });
    worker.join();
    return applied;
}

bool requested() {
    return GetEnvironmentVariableW(L"BTD5ML_DIAG_TYPE_WATCH", nullptr, 0) != 0;
}

std::string hex(const std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", value);
    return text;
}

}  // namespace

TypeWatch::~TypeWatch() { shutdown(); }

std::string TypeWatch::arm(void* const tower) {
    if (tower == nullptr || g_armed != 0 || !requested()) return {};
    if (g_handler == nullptr) g_handler = AddVectoredExceptionHandler(1, &on_exception);
    if (g_handler == nullptr) return "type_watch_unavailable handler";
    for (Slot& slot : g_slots) {
        slot.key = 0;
        slot.hits = 0;
        slot.half_mask = 0;
    }
    g_overflow = 0;
    g_watched = reinterpret_cast<std::uintptr_t>(tower);
    g_copy_site = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + kCopySiteAfter;
    InterlockedExchange(&g_armed, 1);
    const std::size_t threads = apply_debug_registers(g_watched + kTypeOffset, kArmedDr7);
    return "type_watch_armed tower=" + hex(static_cast<std::uint32_t>(g_watched)) +
           ";threads=" + std::to_string(threads);
}

std::vector<std::string> TypeWatch::disarm(const void* const tower) {
    if (g_armed == 0) return {};
    if (tower != nullptr && reinterpret_cast<std::uintptr_t>(tower) != g_watched) return {};
    (void)apply_debug_registers(0, 0);
    InterlockedExchange(&g_armed, 0);

    std::vector<const Slot*> used;
    for (const Slot& slot : g_slots) {
        if (slot.key != 0) used.push_back(&slot);
    }
    std::sort(used.begin(), used.end(), [](const Slot* left, const Slot* right) {
        return left->hits != right->hits ? left->hits > right->hits : left->key < right->key;
    });
    std::vector<std::string> lines;
    lines.push_back("type_watch_summary tower=" + hex(static_cast<std::uint32_t>(g_watched)) +
                    ";sites=" + std::to_string(used.size()) +
                    ";overflow=" + std::to_string(g_overflow));
    for (const Slot* slot : used) {
        const auto key = static_cast<std::uint64_t>(slot->key);
        lines.push_back("type_watch after=" + hex(static_cast<std::uint32_t>(key >> 32)) +
                        ";frame=" + hex(static_cast<std::uint32_t>(key)) +
                        ";half=" + std::to_string(slot->half_mask) +
                        ";hits=" + std::to_string(slot->hits));
    }
    g_watched = 0;
    return lines;
}

void TypeWatch::shutdown() noexcept {
    if (g_armed != 0) {
        try {
            (void)disarm(nullptr);
        } catch (...) {
        }
    }
    if (g_handler != nullptr) {
        RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
    }
}

}  // namespace btd5loader::runtime
