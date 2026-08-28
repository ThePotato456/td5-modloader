#pragma once

#include <filesystem>
#include <mutex>
#include <string_view>

namespace btd5loader::runtime {

class Logger final {
public:
    bool initialize();
    void info(std::string_view component, std::string_view message);
    void error(std::string_view component, std::string_view message);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    void write(std::string_view level, std::string_view component, std::string_view message);

    std::filesystem::path path_;
    std::mutex mutex_;
};

}  // namespace btd5loader::runtime

