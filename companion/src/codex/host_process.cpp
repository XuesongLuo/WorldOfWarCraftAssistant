#include "wowai/codex/host_process.hpp"

#include "wowai/codex/protocol.hpp"

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <map>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace wowai::codex {
namespace {

[[noreturn]] void throw_last_error(const char* operation) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), operation);
}

std::wstring quote_argument(std::wstring_view argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }
    std::wstring result{L'"'};
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring command_line(const HostLaunchOptions& options) {
    std::wstring result = quote_argument(options.executable.native());
    for (const auto& argument : options.arguments) {
        result.push_back(L' ');
        result += quote_argument(argument);
    }
    return result;
}

void make_pipe(wowai::platform::UniqueHandle& read, wowai::platform::UniqueHandle& write) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE raw_read{};
    HANDLE raw_write{};
    if (::CreatePipe(&raw_read, &raw_write, &security, 0) == FALSE) {
        throw_last_error("CreatePipe failed");
    }
    read.reset(raw_read);
    write.reset(raw_write);
}

void make_parent_only(HANDLE handle) {
    if (::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0) == FALSE) {
        throw_last_error("SetHandleInformation failed");
    }
}

struct CaseInsensitiveLess {
    bool operator()(const std::wstring& left, const std::wstring& right) const noexcept {
        return std::lexicographical_compare(
            left.begin(), left.end(), right.begin(), right.end(),
            [](const wchar_t a, const wchar_t b) { return std::towlower(a) < std::towlower(b); });
    }
};

std::vector<wchar_t> child_environment(const HostLaunchOptions& options) {
    std::map<std::wstring, std::wstring, CaseInsensitiveLess> values;
    wchar_t* raw = ::GetEnvironmentStringsW();
    if (raw == nullptr)
        throw_last_error("GetEnvironmentStringsW failed");
    for (const wchar_t* entry = raw; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
        const std::wstring value{entry};
        const auto separator = value.find(L'=', value.starts_with(L'=') ? 1 : 0);
        if (separator != std::wstring::npos)
            values[value.substr(0, separator)] = value.substr(separator + 1);
    }
    ::FreeEnvironmentStringsW(raw);
    for (const auto& name : options.environment_remove)
        values.erase(name);
    for (const auto& [name, value] : options.environment_overrides) {
        if (name.empty() || name.find(L'=') != std::wstring::npos ||
            value.find(L'\0') != std::wstring::npos) {
            throw std::invalid_argument("child environment override is invalid");
        }
        values[name] = value;
    }
    std::vector<wchar_t> block;
    for (const auto& [name, value] : values) {
        block.insert(block.end(), name.begin(), name.end());
        block.push_back(L'=');
        block.insert(block.end(), value.begin(), value.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

} // namespace

HostProcess::HostProcess(const HostLaunchOptions& options)
    : shutdown_timeout_(options.shutdown_timeout) {
    if (options.executable.empty() || options.working_directory.empty()) {
        throw std::invalid_argument("host executable and working directory are required");
    }

    wowai::platform::UniqueHandle child_stdin_read;
    wowai::platform::UniqueHandle child_stdout_write;
    wowai::platform::UniqueHandle child_stderr_write;
    make_pipe(child_stdin_read, stdin_write_);
    make_pipe(stdout_read_, child_stdout_write);
    make_pipe(stderr_read_, child_stderr_write);
    make_parent_only(stdin_write_.get());
    make_parent_only(stdout_read_.get());
    make_parent_only(stderr_read_.get());

    job_.reset(::CreateJobObjectW(nullptr, nullptr));
    if (!job_)
        throw_last_error("CreateJobObjectW failed");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_information{};
    job_information.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (::SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &job_information,
                                  sizeof(job_information)) == FALSE) {
        throw_last_error("SetInformationJobObject failed");
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child_stdin_read.get();
    startup.hStdOutput = child_stdout_write.get();
    startup.hStdError = child_stderr_write.get();
    PROCESS_INFORMATION process_information{};
    std::wstring command = command_line(options);
    std::wstring working_directory = options.working_directory.native();
    auto environment = child_environment(options);
    if (::CreateProcessW(options.executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                         environment.data(), working_directory.c_str(), &startup,
                         &process_information) == FALSE) {
        throw_last_error("CreateProcessW failed");
    }
    process_.reset(process_information.hProcess);
    wowai::platform::UniqueHandle primary_thread{process_information.hThread};
    process_id_ = process_information.dwProcessId;
    child_stdin_read.reset();
    child_stdout_write.reset();
    child_stderr_write.reset();

    if (::AssignProcessToJobObject(job_.get(), process_.get()) == FALSE) {
        ::TerminateProcess(process_.get(), 1);
        throw_last_error("AssignProcessToJobObject failed");
    }
    if (::ResumeThread(primary_thread.get()) == static_cast<DWORD>(-1)) {
        ::TerminateJobObject(job_.get(), 1);
        throw_last_error("ResumeThread failed");
    }
    stderr_thread_ = std::thread([this] { drain_stderr(); });
}

HostProcess::~HostProcess() { shutdown(); }

void HostProcess::write_line(const std::string_view line) {
    if (line.size() > max_message_bytes || line.find_first_of("\r\n") != std::string_view::npos) {
        throw std::invalid_argument("host input must be one bounded JSON line");
    }
    std::string framed(line);
    framed.push_back('\n');
    std::scoped_lock lock(stdin_mutex_);
    std::size_t offset = 0;
    while (offset < framed.size()) {
        DWORD written{};
        const auto remaining = static_cast<DWORD>(
            std::min<std::size_t>(framed.size() - offset, static_cast<std::size_t>(MAXDWORD)));
        if (::WriteFile(stdin_write_.get(), framed.data() + offset, remaining, &written, nullptr) ==
                FALSE ||
            written == 0) {
            throw_last_error("WriteFile to Host failed");
        }
        offset += written;
    }
}

HostReadResult HostProcess::read_line(const std::chrono::milliseconds timeout,
                                      const std::stop_token stop_token) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (stop_token.stop_requested()) {
            return {HostReadStatus::cancelled, {}, STILL_ACTIVE};
        }
        if (const auto newline = stdout_buffer_.find('\n'); newline != std::string::npos) {
            std::string line = stdout_buffer_.substr(0, newline);
            stdout_buffer_.erase(0, newline + 1);
            if (line.find('\r') != std::string::npos) {
                throw std::runtime_error("Host stdout contains CR outside JSON escaping");
            }
            return {HostReadStatus::line, std::move(line), STILL_ACTIVE};
        }
        if (stdout_buffer_.size() > max_message_bytes) {
            throw std::runtime_error("Host stdout line exceeds maximum byte length");
        }

        DWORD available{};
        if (::PeekNamedPipe(stdout_read_.get(), nullptr, 0, nullptr, &available, nullptr) ==
            FALSE) {
            if (::GetLastError() != ERROR_BROKEN_PIPE)
                throw_last_error("PeekNamedPipe failed");
            available = 0;
        }
        if (available != 0) {
            std::array<char, 8'192> chunk{};
            DWORD read{};
            const DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(chunk.size()));
            if (::ReadFile(stdout_read_.get(), chunk.data(), requested, &read, nullptr) == FALSE) {
                if (::GetLastError() != ERROR_BROKEN_PIPE)
                    throw_last_error("ReadFile failed");
            } else {
                stdout_buffer_.append(chunk.data(), read);
            }
            continue;
        }

        DWORD exit_code = STILL_ACTIVE;
        if (!process_ || ::GetExitCodeProcess(process_.get(), &exit_code) == FALSE) {
            if (process_)
                throw_last_error("GetExitCodeProcess failed");
            return {HostReadStatus::exited, {}, exit_code};
        }
        if (exit_code != STILL_ACTIVE)
            return {HostReadStatus::exited, {}, exit_code};
        if (std::chrono::steady_clock::now() >= deadline) {
            return {HostReadStatus::timeout, {}, STILL_ACTIVE};
        }
        ::Sleep(2);
    }
}

bool HostProcess::running() const noexcept {
    DWORD exit_code{};
    return process_ && ::GetExitCodeProcess(process_.get(), &exit_code) != FALSE &&
           exit_code == STILL_ACTIVE;
}

std::string HostProcess::take_stderr() {
    std::scoped_lock lock(stderr_mutex_);
    return std::exchange(stderr_buffer_, {});
}

void HostProcess::shutdown() noexcept {
    if (shutdown_started_)
        return;
    shutdown_started_ = true;
    stdin_write_.reset();
    if (process_ &&
        ::WaitForSingleObject(process_.get(), static_cast<DWORD>(shutdown_timeout_.count())) ==
            WAIT_TIMEOUT) {
        ::TerminateJobObject(job_.get(), 1);
        ::WaitForSingleObject(process_.get(), 2'000);
    }
    process_.reset();
    job_.reset();
    stdout_read_.reset();
    if (stderr_thread_.joinable())
        stderr_thread_.join();
    stderr_read_.reset();
}

void HostProcess::drain_stderr() noexcept {
    std::array<char, 4'096> chunk{};
    while (stderr_read_) {
        DWORD read{};
        if (::ReadFile(stderr_read_.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read,
                       nullptr) == FALSE ||
            read == 0) {
            return;
        }
        std::scoped_lock lock(stderr_mutex_);
        constexpr std::size_t maximum_diagnostics = 64 * 1'024;
        const std::size_t remaining =
            maximum_diagnostics - std::min(maximum_diagnostics, stderr_buffer_.size());
        stderr_buffer_.append(chunk.data(), std::min<std::size_t>(read, remaining));
    }
}

} // namespace wowai::codex
