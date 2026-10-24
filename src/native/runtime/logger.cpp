#include "logger.hpp"

#include <Windows.h>

#include <array>
#include <string>

namespace btd5loader::runtime {
namespace {

std::string escape_json(const std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output += character;
            break;
        }
    }
    return output;
}

std::wstring environment_value(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }

    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required) {
        return {};
    }
    value.resize(written);
    return value;
}

std::filesystem::path data_root() {
    const std::wstring override_path = environment_value(L"BTD5ML_DATA_ROOT");
    if (!override_path.empty()) {
        return override_path;
    }

    const std::wstring local_app_data = environment_value(L"LOCALAPPDATA");
    if (local_app_data.empty()) {
        return {};
    }
    return std::filesystem::path(local_app_data) / L"BTD5ModLoader";
}

std::string timestamp_utc() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::array<char, 32> timestamp{};
    const int written = sprintf_s(
        timestamp.data(),
        timestamp.size(),
        "%04hu-%02hu-%02huT%02hu:%02hu:%02hu.%03huZ",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds);
    return written > 0
               ? std::string(timestamp.data(), static_cast<std::size_t>(written))
               : std::string{};
}

constexpr HANDLE as_handle(void* const handle) noexcept {
    return static_cast<HANDLE>(handle);
}

}  // namespace

Logger::Logger(const std::uint64_t maximum_bytes) noexcept
    : maximum_bytes_(maximum_bytes == 0 ? kDefaultMaximumBytes : maximum_bytes) {}

Logger::~Logger() {
    std::scoped_lock lock(mutex_);
    close_locked();
}

bool Logger::initialize() {
    const auto root = data_root();
    if (root.empty()) {
        return false;
    }

    std::error_code error;
    const auto log_directory = root / L"logs";
    std::filesystem::create_directories(log_directory, error);
    if (error) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    close_locked();
    path_ = log_directory / kFileName;
    rotated_path_ = log_directory / kRotatedFileName;
    return open_locked();
}

void Logger::info(const std::string_view component, const std::string_view message) {
    write("info", component, message);
}

void Logger::error(const std::string_view component, const std::string_view message) {
    write("error", component, message);
}

const std::filesystem::path& Logger::path() const noexcept {
    return path_;
}

bool Logger::open_locked() noexcept {
    if (handle_ != nullptr) {
        return true;
    }
    // FILE_APPEND_DATA without FILE_WRITE_DATA makes every write land at the
    // current end of file, even after another process truncates the log.
    const HANDLE handle = CreateFileW(
        path_.c_str(),
        FILE_APPEND_DATA | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    handle_ = handle;
    return true;
}

void Logger::close_locked() noexcept {
    if (handle_ != nullptr) {
        CloseHandle(as_handle(handle_));
        handle_ = nullptr;
    }
}

void Logger::rotate_locked() noexcept {
    close_locked();
    if (MoveFileExW(path_.c_str(), rotated_path_.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
        // Another process holds the log without delete sharing. Truncate in
        // place so the size cap still holds.
        const HANDLE truncate = CreateFileW(
            path_.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            TRUNCATE_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (truncate != INVALID_HANDLE_VALUE) {
            CloseHandle(truncate);
        }
    }
    (void)open_locked();
}

void Logger::write(
    const std::string_view level,
    const std::string_view component,
    const std::string_view message) {
    if (path_.empty()) {
        return;
    }

    std::string record;
    record.reserve(64 + component.size() + message.size());
    record += "{\"timestamp\":\"";
    record += timestamp_utc();
    record += "\",\"level\":\"";
    record += escape_json(level);
    record += "\",\"component\":\"";
    record += escape_json(component);
    record += "\",\"message\":\"";
    record += escape_json(message);
    record += "\"}\n";

    std::scoped_lock lock(mutex_);
    if (!open_locked()) {
        return;
    }
    LARGE_INTEGER size{};
    // Query the size each time: the manager may clear the file externally.
    if (GetFileSizeEx(as_handle(handle_), &size) != FALSE && size.QuadPart > 0 &&
        static_cast<std::uint64_t>(size.QuadPart) + record.size() > maximum_bytes_) {
        rotate_locked();
        if (handle_ == nullptr) {
            return;
        }
    }
    DWORD written = 0;
    if (WriteFile(
            as_handle(handle_),
            record.data(),
            static_cast<DWORD>(record.size()),
            &written,
            nullptr) == FALSE) {
        // A stale handle (for example after the file was deleted) is reopened
        // on the next record.
        close_locked();
    }
}

}  // namespace btd5loader::runtime
