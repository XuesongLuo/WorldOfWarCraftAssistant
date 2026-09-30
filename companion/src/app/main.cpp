#include "wowai/app/application_shell.hpp"
#include "wowai/platform/resources.hpp"
#include "wowai/platform/single_instance.hpp"

#include <exception>
#include <string>

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        wowai::platform::SingleInstance single_instance{wowai::app::single_instance_mutex};
        if (!single_instance.is_primary()) {
            const bool activated = wowai::platform::SingleInstance::notify_existing_window(
                wowai::app::application_window_class,
                wowai::app::activate_existing_instance_message);
            if (!activated) {
                ::MessageBoxW(nullptr, L"The existing assistant instance is still starting.",
                              L"World of Warcraft AI Assistant", MB_OK | MB_ICONINFORMATION);
            }
            return activated ? 0 : 2;
        }

        auto com_apartment = wowai::platform::initialize_sta();
        wowai::app::ApplicationShell application{instance};
        return application.run();
    } catch (const std::exception& error) {
        const std::string message = error.what();
        const std::wstring wide_message(message.begin(), message.end());
        ::MessageBoxW(nullptr, wide_message.c_str(), L"Assistant startup failed",
                      MB_OK | MB_ICONERROR);
        return 1;
    } catch (...) {
        ::MessageBoxW(nullptr, L"An unknown startup error occurred.", L"Assistant startup failed",
                      MB_OK | MB_ICONERROR);
        return 1;
    }
}
