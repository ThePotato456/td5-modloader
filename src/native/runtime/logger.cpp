#include "logger.hpp"

#include <Windows.h>

#include <array>
#include <fstream>
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

}  // namespace

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

    path_ = log_directory / L"runtime.jsonl";
    std::ofstream stream(path_, std::ios::app | std::ios::binary);
    return stream.good();
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

void Logger::write(
    const std::string_view level,
    const std::string_view component,
    const std::string_view message) {
    if (path_.empty()) {
        return;
    }

    std::scoped_lock lock(mutex_);
    std::ofstream stream(path_, std::ios::app | std::ios::binary);
    if (!stream) {
        return;
    }

    stream << "{\"timestamp\":\"" << timestamp_utc()
           << "\",\"level\":\"" << escape_json(level)
           << "\",\"component\":\"" << escape_json(component)
           << "\",\"message\":\"" << escape_json(message)
           << "\"}\n";
}

}  // namespace btd5loader::runtime
