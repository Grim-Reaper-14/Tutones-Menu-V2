#pragma once

#include "../memory/ModuleView.hpp"
#include "../types/ScriptTypes.hpp"
#include "NativeRegistry.hpp"

#include <atomic>

namespace TutonesV2::Game::Native
{
    using RunScriptThreadsFn = bool(*)(int operationsToExecute);

    class NativePointers final
    {
    public:
        static NativePointers& Get() noexcept;

        bool Resolve() noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool IsResolved() const noexcept;
        [[nodiscard]] InitNativeTablesFn InitNativeTables() const noexcept;
        [[nodiscard]] RunScriptThreadsFn RunScriptThreads() const noexcept;
        [[nodiscard]] Types::AtArray<Types::ScriptThread*>* ScriptThreads() const noexcept;
        [[nodiscard]] std::int64_t** ScriptGlobals() const noexcept;
        [[nodiscard]] bool* IsSessionStarted() const noexcept;
        [[nodiscard]] std::uint32_t* NetworkTime() const noexcept;

    private:
        Memory::ModuleView m_Module;
        InitNativeTablesFn m_InitNativeTables{};
        RunScriptThreadsFn m_RunScriptThreads{};
        Types::AtArray<Types::ScriptThread*>* m_ScriptThreads{};
        std::int64_t** m_ScriptGlobals{};
        bool* m_IsSessionStarted{};
        std::uint32_t* m_NetworkTime{};
        std::atomic_bool m_Resolved{};
    };
}
