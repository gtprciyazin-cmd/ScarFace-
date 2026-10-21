#include <csignal>
#include <iostream>
#include <string>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h> // ShellExecuteExW (WIN32_LEAN_AND_MEAN drops it from windows.h)
#ifdef _MSC_VER
#pragma comment(lib, "Shell32.lib")  // ShellExecuteExW
#pragma comment(lib, "Advapi32.lib") // OpenProcessToken / GetTokenInformation
#endif
#endif

#include "core/branding.hpp"
#include "core/core.hpp"
#include "core/embedded_resources.hpp"
#include "core/firewall.hpp"
#include "core/hosts_redirect.hpp"
#include "core/logger.hpp"
#include "license/license_guard.hpp"

std::unique_ptr<core::Core> g_core{ nullptr };
// Non-owning; points at the stack object in main() while the proxy runs, so the
// console-close handler can undo the hosts redirect before Windows kills us.
core::HostsRedirect* g_hosts{ nullptr };

namespace {
#ifdef _WIN32
// Routing Growtopia through the proxy means writing the Windows hosts file
// (C:\Windows\System32\drivers\etc\hosts) and binding port 443 -- both need
// administrator rights. When launched without them (a normal double-click),
// relaunch ourselves through the UAC prompt and let that elevated copy do the
// work. Returns true when this process is already elevated and should carry on;
// false when an elevated copy was started (or the user declined) and this
// process must exit. The build also asks for elevation via the exe manifest,
// but this runtime check still routes the game even when the manifest is
// stripped or the exe is started in a way that skips it.
[[nodiscard]] bool ensure_elevated()
{
    bool elevated{ false };
    HANDLE token{ nullptr };
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size{ sizeof(elevation) };
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            elevated = elevation.TokenIsElevated != 0;
        }
        CloseHandle(token);
    }

    if (elevated) {
        return true;
    }

    wchar_t path[MAX_PATH]{};
    const DWORD len{ GetModuleFileNameW(nullptr, path, MAX_PATH) };
    if (len == 0 || len >= MAX_PATH) {
        // Can't find our own path to relaunch; carry on and let the hosts write
        // fail with its own warning rather than refusing to start at all.
        return true;
    }

    // Start the elevated copy in the exe's own folder so config.json and
    // resources/ resolve next to it instead of in System32.
    std::wstring directory{ path };
    if (const auto slash{ directory.find_last_of(L"\\/") }; slash != std::wstring::npos) {
        directory.resize(slash);
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas"; // triggers the Windows UAC elevation prompt
    sei.lpFile = path;
    sei.lpDirectory = directory.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            CloseHandle(sei.hProcess);
        }
        return false; // the elevated copy is running; this one quits quietly
    }

    // ShellExecuteExW failed, most often because the user clicked "No" on UAC.
    std::cout
        << "\nScarface Proxy needs administrator rights to route Growtopia through it.\n"
           "Click \"Yes\" on the Windows prompt, or right-click the exe and choose\n"
           "\"Run as administrator\".\n"
           "\nPress Enter to close..."
        << std::flush;
    std::string discard{};
    std::getline(std::cin, discard);
    return false;
}
#endif

// True once ANSI colour escape sequences are known to render (always on
// non-Windows; on Windows only after virtual-terminal mode is enabled).
bool g_ansi{
#ifdef _WIN32
    false
#else
    true
#endif
};

// Make the console show the UTF-8 logo and ANSI colours.
void setup_console()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    if (const HANDLE out{ GetStdHandle(STD_OUTPUT_HANDLE) }; out && out != INVALID_HANDLE_VALUE) {
        DWORD mode{ 0 };
        if (GetConsoleMode(out, &mode)
            && SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            g_ansi = true;
        }
    }
#endif
}

// The Scarface logo + tagline, shown above the license-key prompt.
void print_banner()
{
    const char* red{ g_ansi ? "\x1b[1;91m" : "" };
    const char* white{ g_ansi ? "\x1b[1;97m" : "" };
    const char* gray{ g_ansi ? "\x1b[90m" : "" };
    const char* reset{ g_ansi ? "\x1b[0m" : "" };

    std::cout << '\n'
              << red << core::branding::LOGO << reset << '\n'
              << "          " << white << "PROXY " << core::branding::VERSION << reset
              << "   " << gray << '"' << core::branding::TAGLINE << '"' << reset
              << "\n\n" << std::flush;
}

// Wipe the console (including scrollback). Uses the console API on Windows so
// it also works where ANSI escapes do not (Windows 7/8).
void clear_console()
{
#ifdef _WIN32
    const HANDLE out{ GetStdHandle(STD_OUTPUT_HANDLE) };
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!out || out == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(out, &info)) {
        return;
    }

    const DWORD cells{ static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y) };
    const COORD origin{ 0, 0 };
    DWORD written{ 0 };
    FillConsoleOutputCharacterA(out, ' ', cells, origin, &written);
    FillConsoleOutputAttribute(out, info.wAttributes, cells, origin, &written);
    SetConsoleCursorPosition(out, origin);
#else
    std::cout << "\x1b[2J\x1b[3J\x1b[H" << std::flush;
#endif
}

// When the exe is double-clicked, Windows closes its console the instant the
// program returns, so any error (a rejected key, an unreachable license server)
// flashes by unread. Keep the window open in that case. Launched from an
// existing terminal, the console has other processes attached, so skip the
// wait. No-op on non-Windows.
void pause_if_double_click()
{
#ifdef _WIN32
    DWORD pids[2];
    if (GetConsoleProcessList(pids, 2) > 1) {
        return;
    }

    std::cout << "\nPress Enter to close..." << std::flush;
    std::string discard;
    std::getline(std::cin, discard);
#endif
}
}

void signal_handler(int signal)
{
    if (!g_core) {
        return;
    }

    spdlog::info("Interrupted by signal {}, stopping...", signal);
    g_core->stop();
}

#ifdef _WIN32
// Also fires when the user clicks the console's X button or logs off, which
// std::signal does not catch. Undo the hosts redirect first so the machine is
// never left pointing Growtopia at a proxy that is no longer running.
BOOL WINAPI console_ctrl_handler(const DWORD ctrl_type)
{
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_hosts) {
            g_hosts->restore();
        }
        if (g_core) {
            g_core->stop();
        }
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

int main()
{
#ifdef _WIN32
    // Elevate before anything else: a non-elevated copy hands off to an elevated
    // one and exits, so only the elevated process sets up the console and runs.
    if (!ensure_elevated()) {
        return 0;
    }
    SetConsoleTitleA(std::string{ core::branding::PRODUCT_NAME }.c_str());
#endif
    setup_console();
    print_banner();

    try {
        core::Logger logger{};

        std::vector<spdlog::sink_ptr> sinks{
            core::Logger::create_console_sink(),
            core::Logger::create_file_sink()
        };

        logger.set_logger(std::make_shared<spdlog::logger>("ScarfaceProxy", sinks.begin(), sinks.end()));
        logger.get_logger()->set_level(spdlog::level::trace);

        spdlog::register_logger(logger.get_logger());
        spdlog::set_default_logger(logger.get_logger());
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] %v");
    }
    catch (const spdlog::spdlog_ex& ex) {
        spdlog::error("Log initialization failed: {}", ex.what());
        return 1;
    }

    int exit_code{ 0 };
    try {
        spdlog::info(
            "Starting {} (v{}.{}.{}{})",
            core::branding::PRODUCT_NAME,
            GTPROXY_VERSION_MAJOR,
            GTPROXY_VERSION_MINOR,
            GTPROXY_VERSION_PATCH,
#ifdef GTPROXY_BUILD_VERSION
            fmt::format("-{}", GTPROXY_BUILD_VERSION)
#else
            ""
#endif
        );

        std::optional<license::Activation> activation{};
        if constexpr (license::ENABLED) {
            activation = license::check_on_startup();
            if (!activation) {
                pause_if_double_click();
                return 1;
            }

            // Key accepted: drop the prompt from the screen and start the
            // proxy logs on a clean console under the logo.
            clear_console();
            print_banner();
            license::log_activation(*activation);
        }
        else {
            spdlog::warn("License check disabled in this build");
        }

        // Write the files the HTTPS web server needs if they are missing,
        // so the proxy runs as a single exe.
        core::ensure_embedded_resources();
        core::allow_through_firewall();

        g_core = std::make_unique<core::Core>();

        std::shared_ptr<license::LicenseMonitor> license_monitor{};
        if (activation) {
            license_monitor = std::make_shared<license::LicenseMonitor>(
                g_core->get_scheduler(),
                std::move(*activation),
                [] { g_core->stop(); }
            );
            license_monitor->start();
        }

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Route Growtopia through the proxy automatically (and undo it on exit),
        // so the user never has to edit the hosts file by hand. Done after the
        // core is up, so 127.0.0.1 is already listening.
        core::HostsRedirect hosts_redirect{};
        g_hosts = &hosts_redirect;
#ifdef _WIN32
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#endif

        g_core->run();

        g_hosts = nullptr;
        hosts_redirect.restore();
    }
    catch (const std::runtime_error& e) {
        spdlog::error("Runtime error: {}", e.what());
        exit_code = 1;
    }
    catch (const std::exception& ex) {
        spdlog::error("Exception: {}", ex.what());
        exit_code = 1;
    }

    // Destroy the core while logging still works: its scheduler logs when it
    // stops. Left to static destruction it ran after spdlog::shutdown() and crashed.
    g_core.reset();

    spdlog::info("{} stopped", core::branding::PRODUCT_NAME);
    spdlog::shutdown();

    pause_if_double_click();
    return exit_code;
}
