#pragma once

#include "wowai/platform/resources.hpp"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <windows.h>

namespace wowai::codex {

struct HostLaunchOptions {
    std::filesystem::path executable;
    std::vector<std::wstring> arguments;
    std::filesystem::path working_directory;
    std::chrono::milliseconds shutdown_timeout{2'000};
    std::vector<std::pair<std::wstring, std::wstring>> environment_overrides;
    std::vector<std::wstring> environment_remove;
};

enum class HostReadStatus { line, timeout, exited, cancelled };

struct HostReadResult {
    HostReadStatus status{HostReadStatus::exited};
    std::string line;
    DWORD exit_code{};
};

class HostProcess final {
  public:
    explicit HostProcess(const HostLaunchOptions& options);
    ~HostProcess();

    HostProcess(const HostProcess&) = delete;
    HostProcess& operator=(const HostProcess&) = delete;

    void write_line(std::string_view line);
    [[nodiscard]] HostReadResult read_line(std::chrono::milliseconds timeout,
                                           std::stop_token stop_token = {});
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] DWORD process_id() const noexcept { return process_id_; }
    [[nodiscard]] std::string take_stderr();
    void shutdown() noexcept;

  private:
    void drain_stderr() noexcept;

    wowai::platform::UniqueHandle job_;
    wowai::platform::UniqueHandle process_;
    wowai::platform::UniqueHandle stdin_write_;
    wowai::platform::UniqueHandle stdout_read_;
    wowai::platform::UniqueHandle stderr_read_;
    std::chrono::milliseconds shutdown_timeout_;
    std::mutex stdin_mutex_;
    DWORD process_id_{};
    std::string stdout_buffer_;
    std::mutex stderr_mutex_;
    std::string stderr_buffer_;
    std::thread stderr_thread_;
    bool shutdown_started_{false};
};

} // namespace wowai::codex
