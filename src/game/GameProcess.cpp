#include "GameProcess.hpp"

#include <cwchar>

namespace TutonesV2::Game
{
    HMODULE GameProcess::MainModule() noexcept
    {
        return ::GetModuleHandleW(nullptr);
    }

    bool GameProcess::IsEnhancedHost() noexcept
    {
        wchar_t modulePath[32768]{};
        const DWORD length = ::GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(sizeof(modulePath) / sizeof(modulePath[0])));
        if (length == 0 || length >= (sizeof(modulePath) / sizeof(modulePath[0])))
            return false;

        const wchar_t* filename = std::wcsrchr(modulePath, L'\\');
        filename = filename ? filename + 1 : modulePath;
        return ::_wcsicmp(filename, L"GTA5_Enhanced.exe") == 0;
    }
}
