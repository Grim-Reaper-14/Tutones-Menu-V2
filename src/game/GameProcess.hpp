#pragma once

#include <Windows.h>

namespace TutonesV2::Game
{
    class GameProcess final
    {
    public:
        static bool IsEnhancedHost() noexcept;
        static HMODULE MainModule() noexcept;
    };
}
