#include "firewall.hpp"

#include <string>

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace core {
#ifdef _WIN32
namespace {
    // Runs netsh without a console window; returns its exit code (-1 on failure).
    int run_netsh(const std::wstring& arguments)
    {
        std::wstring command_line{ L"netsh.exe " + arguments };
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            return -1;
        }

        WaitForSingleObject(process.hProcess, 15000);
        DWORD exit_code{ 0 };
        const bool done{ GetExitCodeProcess(process.hProcess, &exit_code) && exit_code != STILL_ACTIVE };
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return done ? static_cast<int>(exit_code) : -1;
    }
}

void allow_through_firewall()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length{ GetModuleFileNameW(nullptr, path, MAX_PATH) };
    if (length == 0 || length >= MAX_PATH) {
        return;
    }

    // Before any socket opens: with a rule in place Windows never shows the
    // prompt, and until a prompt is answered it drops the server's replies.
    const std::wstring program{ L"program=\"" + std::wstring{ path, length } + L"\"" };
    // "No rules match" is fine here; only the adds have to work.
    run_netsh(L"advfirewall firewall delete rule name=all " + program);
    const int in{ run_netsh(L"advfirewall firewall add rule name=\"Scarface Proxy\" dir=in action=allow enable=yes profile=any " + program) };
    const int out{ run_netsh(L"advfirewall firewall add rule name=\"Scarface Proxy\" dir=out action=allow enable=yes profile=any " + program) };

    if (in == 0 && out == 0) {
        spdlog::info("Windows Firewall: Scarface Proxy is allowed");
    }
    else {
        spdlog::warn("Could not add the Windows Firewall rule; if the game does not connect, allow ScarfaceProxy.exe in the firewall by hand.");
    }
}
#else
void allow_through_firewall()
{
}
#endif
}
