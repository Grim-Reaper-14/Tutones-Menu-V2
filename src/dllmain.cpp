#include "app/Application.hpp"

#include <Windows.h>

#include <filesystem>

namespace
{
    HMODULE g_Module{};

    DWORD WINAPI BootstrapThread(void*)
    {
        wchar_t modulePath[MAX_PATH]{};
        const DWORD length = ::GetModuleFileNameW(g_Module, modulePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return 1;

        const std::filesystem::path directory = std::filesystem::path(modulePath).parent_path();
        if (!TutonesV2::App::Application::Get().Initialize(directory))
            return 2;

        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_Module = module;
        ::DisableThreadLibraryCalls(module);

        if (HANDLE thread = ::CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr))
            ::CloseHandle(thread);
    }

    // Do not run service teardown under the Windows loader lock. V2 will call
    // Application::Shutdown() from its explicit unload path once that path exists.
    return TRUE;
}
