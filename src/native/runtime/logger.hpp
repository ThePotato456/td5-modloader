#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string_view>

namespace btd5loader::runtime {

// Appends structured JSON lines to `<data root>/logs/runtime.jsonl` through one
// persistent append-only handle. When a record would grow the file past the
// size cap, the file is rotated to `runtime.1.jsonl` (replacing any previous
// rotation), so the log never uses more than about twice the cap. The handle
// shares read, write, and delete access so the manager can read, clear, or
// remove the log while the game runs.
class Logger final {
public:
    static constexpr std::uint64_t kDefaultMaximumBytes = 8ULL * 1024ULL * 1024ULL;
    static constexpr const wchar_t* kFileName = L"runtime.jsonl";
    static constexpr const wchar_t* kRotatedFileName = L"runtime.1.jsonl";

    explicit Logger(std::uint64_t maximum_bytes = kDefaultMaximumBytes) noexcept;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool initialize();
    void info(std::string_view component, std::string_view message);
    void error(std::string_view component, std::string_view message);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    void write(std::string_view level, std::string_view component, std::string_view message);
    [[nodiscard]] bool open_locked() noexcept;
    void close_locked() noexcept;
    void rotate_locked() noexcept;

    std::filesystem::path path_;
    std::filesystem::path rotated_path_;
    std::uint64_t maximum_bytes_;
    void* handle_{};
    std::mutex mutex_;
};

}  // namespace btd5loader::runtime
