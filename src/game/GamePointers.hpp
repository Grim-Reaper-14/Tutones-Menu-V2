#pragma once

#include "native/NativePointers.hpp"

namespace TutonesV2::Game
{
    using InitNativeTablesFn = Native::InitNativeTablesFn;
    using RunScriptThreadsFn = Native::RunScriptThreadsFn;

    class GamePointers final
    {
    public:
        static GamePointers& Get() noexcept
        {
            static GamePointers instance;
            return instance;
        }

        [[nodiscard]] bool IsResolved() const noexcept
        {
            return Native::NativePointers::Get().IsResolved();
        }

        [[nodiscard]] InitNativeTablesFn InitNativeTables() const noexcept
        {
            return Native::NativePointers::Get().InitNativeTables();
        }

        [[nodiscard]] RunScriptThreadsFn RunScriptThreads() const noexcept
        {
            return Native::NativePointers::Get().RunScriptThreads();
        }

        [[nodiscard]] Types::AtArray<Types::ScriptThread*>* ScriptThreads() const noexcept
        {
            return Native::NativePointers::Get().ScriptThreads();
        }

        [[nodiscard]] std::int64_t** ScriptGlobals() const noexcept
        {
            return Native::NativePointers::Get().ScriptGlobals();
        }

        [[nodiscard]] bool* IsSessionStarted() const noexcept
        {
            return Native::NativePointers::Get().IsSessionStarted();
        }

        [[nodiscard]] std::uint32_t* NetworkTime() const noexcept
        {
            return Native::NativePointers::Get().NetworkTime();
        }

    private:
        GamePointers() = default;
    };
}
