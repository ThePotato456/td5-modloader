#pragma once

#include <Windows.h>

#include <cstdint>

namespace btd5loader::runtime {

inline constexpr char kInitializeExport[] = "BTD5Loader_Initialize";
inline constexpr char kGetStateExport[] = "BTD5Loader_GetState";
inline constexpr char kShutdownExport[] = "BTD5Loader_Shutdown";

enum class State : std::uint32_t {
    NotStarted = 0,
    Bootstrap,
    CompatibilityCheck,
    HooksReady,
    ModsLoading,
    GameReady,
    ShuttingDown,
    Failed,
};

using InitializeFunction = BOOL(WINAPI*)(const wchar_t* game_directory);
using GetStateFunction = State(WINAPI*)();
using ShutdownFunction = void(WINAPI*)();

}  // namespace btd5loader::runtime

