#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <fcntl.h>
#include <io.h>
#include <windows.h>

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    const std::wstring mode = argc > 1 ? argv[1] : L"";
    if (mode == L"--crash") return 23;
    if (mode == L"--linger") {
        while (true) ::Sleep(1'000);
    }
    if (mode == L"--spawn-descendant") {
        std::wstring command = L"\"" + std::wstring(argv[0]) + L"\" --linger";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (::CreateProcessW(argv[0], command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &startup, &process) == FALSE) {
            return 4;
        }
        ::CloseHandle(process.hThread);
        std::cout << process.dwProcessId << '\n' << std::flush;
        ::CloseHandle(process.hProcess);
        while (true) ::Sleep(1'000);
    }

    std::string line;
    if (!std::getline(std::cin, line)) return 5;
    if (mode == L"--handshake") {
        std::cout << R"({"protocolVersion":"2.0","messageId":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa","kind":"ready","requestId":null,"sequence":1,"sentAt":"2026-10-03T00:00:00Z","timeoutMs":null,"payload":{"selectedVersion":"2.0","maxMessageBytes":1048576}})"
                  << '\n'
                  << std::flush;
        while (std::getline(std::cin, line)) {
        }
        return 0;
    }
    if (mode == L"--split") {
        const auto middle = line.size() / 2;
        std::cout << line.substr(0, middle) << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::cout << line.substr(middle) << '\n' << std::flush;
        return 0;
    }
    if (mode == L"--sticky") {
        std::cout << "first\nsecond\n" << std::flush;
        return 0;
    }
    if (mode == L"--silent") {
        ::Sleep(10'000);
        return 0;
    }
    return 6;
}
