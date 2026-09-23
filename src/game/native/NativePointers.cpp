#include "NativePointers.hpp"

#include "../memory/PatternScanner.hpp"
#include "../../core/Logger.hpp"

#include <cstdint>
#include <format>

namespace TutonesV2::Game::Native
{
    NativePointers& NativePointers::Get() noexcept
    {
        static NativePointers instance;
        return instance;
    }

    bool NativePointers::Resolve() noexcept
    {
        if (m_Resolved.load(std::memory_order_acquire))
            return true;

        Reset();
        if (!m_Module.Initialize(L"GTA5_Enhanced.exe"))
        {
            Core::Logger::Get().Error("native.ptr", "Could not inspect GTA5_Enhanced.exe module image");
            return false;
        }

        constexpr auto initNativeTablesPattern = "EB 2A 0F 1F 40 00 48 8B 54 17 10";
        auto* initNativeMatch = Memory::PatternScanner::FindFirst(m_Module, initNativeTablesPattern);
        if (!initNativeMatch)
        {
            Core::Logger::Get().Error("native.ptr", "InitNativeTables pattern was not found");
            Reset();
            return false;
        }

        auto* initNativeAddress = initNativeMatch - 0x2A;
        if (!m_Module.Contains(initNativeAddress))
        {
            Core::Logger::Get().Error("native.ptr", "InitNativeTables resolved outside GTA module image");
            Reset();
            return false;
        }
        m_InitNativeTables = reinterpret_cast<InitNativeTablesFn>(initNativeAddress);

        constexpr auto runScriptThreadsPattern = "BE 40 5D C6 00";
        auto* runScriptThreadsMatch = Memory::PatternScanner::FindFirst(m_Module, runScriptThreadsPattern);
        if (!runScriptThreadsMatch)
        {
            Core::Logger::Get().Error("native.ptr", "RunScriptThreads pattern was not found");
            Reset();
            return false;
        }

        auto* runScriptThreadsAddress = runScriptThreadsMatch - 0x0A;
        if (!m_Module.Contains(runScriptThreadsAddress))
        {
            Core::Logger::Get().Error("native.ptr", "RunScriptThreads resolved outside GTA module image");
            Reset();
            return false;
        }
        m_RunScriptThreads = reinterpret_cast<RunScriptThreadsFn>(runScriptThreadsAddress);

        constexpr auto scriptThreadsPattern = "48 8B 05 ? ? ? ? 48 89 34 F8 48 FF C7 48 39 FB 75 97";
        auto* scriptThreadsMatch = Memory::PatternScanner::FindFirst(m_Module, scriptThreadsPattern);
        if (!scriptThreadsMatch)
        {
            Core::Logger::Get().Error("native.ptr", "ScriptThreads pattern was not found");
            Reset();
            return false;
        }

        auto* scriptThreadsAddress = Memory::PatternScanner::ResolveRip(scriptThreadsMatch + 3);
        if (!scriptThreadsAddress || !m_Module.Contains(scriptThreadsAddress))
        {
            Core::Logger::Get().Error("native.ptr", "ScriptThreads pointer resolved outside GTA module image");
            Reset();
            return false;
        }
        m_ScriptThreads = reinterpret_cast<Types::AtArray<Types::ScriptThread*>*>(scriptThreadsAddress);

        constexpr auto scriptGlobalsPattern = "48 8B 8E B8 00 00 00 48 8D 15 ? ? ? ? 49 89 D8";
        if (auto* match = Memory::PatternScanner::FindFirst(m_Module, scriptGlobalsPattern))
        {
            auto* address = Memory::PatternScanner::ResolveRip(match + 0x0A);
            if (address && m_Module.Contains(address))
                m_ScriptGlobals = reinterpret_cast<std::int64_t**>(address);
            else
                Core::Logger::Get().Warn("native.ptr", "ScriptGlobals resolved outside GTA module image");
        }
        else
        {
            Core::Logger::Get().Warn("native.ptr", "ScriptGlobals pattern was not found; Online Self globals disabled");
        }

        constexpr auto scriptProgramsPattern = "48 C7 84 C8 D8 00 00 00 00 00 00 00";
        if (auto* match = Memory::PatternScanner::FindFirst(m_Module, scriptProgramsPattern))
        {
            auto* base = Memory::PatternScanner::ResolveRip(match + 0x16);
            auto* address = base ? base + 0xD8 : nullptr;
            if (address && m_Module.Contains(address))
                m_ScriptPrograms = reinterpret_cast<Types::ScriptProgram**>(address);
            else
                Core::Logger::Get().Warn("native.ptr", "ScriptPrograms resolved outside GTA module image");
        }
        else
        {
            Core::Logger::Get().Warn("native.ptr", "ScriptPrograms pattern was not found; Online session switching disabled");
        }

        constexpr auto scriptVmPattern = "49 63 41 1C";
        if (auto* match = Memory::PatternScanner::FindFirst(m_Module, scriptVmPattern))
        {
            auto* address = match - 0x24;
            if (m_Module.Contains(address))
                m_ScriptVm = reinterpret_cast<ScriptVmFn>(address);
            else
                Core::Logger::Get().Warn("native.ptr", "ScriptVM resolved outside GTA module image");
        }
        else
        {
            Core::Logger::Get().Warn("native.ptr", "ScriptVM pattern was not found; Online session switching disabled");
        }

        constexpr auto sessionStartedPattern = "0F B6 05 ? ? ? ? 0A 05 ? ? ? ? 75 2A";
        if (auto* match = Memory::PatternScanner::FindFirst(m_Module, sessionStartedPattern))
        {
            auto* address = Memory::PatternScanner::ResolveRip(match + 3);
            if (address && m_Module.Contains(address))
                m_IsSessionStarted = reinterpret_cast<bool*>(address);
            else
                Core::Logger::Get().Warn("native.ptr", "IsSessionStarted resolved outside GTA module image");
        }
        else
        {
            Core::Logger::Get().Warn("native.ptr", "IsSessionStarted pattern was not found; Online Self globals disabled");
        }

        constexpr auto networkTimePattern = "89 05 ? ? ? ? 80 3D ? ? ? ? ? 0F 84 ? ? ? ? E9";
        if (auto* match = Memory::PatternScanner::FindFirst(m_Module, networkTimePattern))
        {
            auto* address = Memory::PatternScanner::ResolveRip(match + 2);
            if (address && m_Module.Contains(address))
                m_NetworkTime = reinterpret_cast<std::uint32_t*>(address);
            else
                Core::Logger::Get().Warn("native.ptr", "NetworkTime resolved outside GTA module image");
        }
        else
        {
            Core::Logger::Get().Warn("native.ptr", "NetworkTime pattern was not found; Off Radar disabled");
        }

        m_Resolved.store(true, std::memory_order_release);
        Core::Logger::Get().Info(
            "native.ptr",
            std::format(
                "Native runtime pointers resolved once: InitNativeTables=0x{:X}, RunScriptThreads=0x{:X}, ScriptThreads=0x{:X}",
                reinterpret_cast<std::uintptr_t>(m_InitNativeTables),
                reinterpret_cast<std::uintptr_t>(m_RunScriptThreads),
                reinterpret_cast<std::uintptr_t>(m_ScriptThreads)));
        return true;
    }

    void NativePointers::Reset() noexcept
    {
        m_Resolved.store(false, std::memory_order_release);
        m_InitNativeTables = nullptr;
        m_RunScriptThreads = nullptr;
        m_ScriptThreads = nullptr;
        m_ScriptPrograms = nullptr;
        m_ScriptGlobals = nullptr;
        m_ScriptVm = nullptr;
        m_IsSessionStarted = nullptr;
        m_NetworkTime = nullptr;
        m_Module.Reset();
    }

    bool NativePointers::IsResolved() const noexcept
    {
        return m_Resolved.load(std::memory_order_acquire);
    }

    InitNativeTablesFn NativePointers::InitNativeTables() const noexcept
    {
        return m_InitNativeTables;
    }

    RunScriptThreadsFn NativePointers::RunScriptThreads() const noexcept
    {
        return m_RunScriptThreads;
    }

    Types::AtArray<Types::ScriptThread*>* NativePointers::ScriptThreads() const noexcept
    {
        return m_ScriptThreads;
    }

    Types::ScriptProgram** NativePointers::ScriptPrograms() const noexcept
    {
        return m_ScriptPrograms;
    }

    std::int64_t** NativePointers::ScriptGlobals() const noexcept
    {
        return m_ScriptGlobals;
    }

    ScriptVmFn NativePointers::ScriptVm() const noexcept
    {
        return m_ScriptVm;
    }

    bool* NativePointers::IsSessionStarted() const noexcept
    {
        return m_IsSessionStarted;
    }

    std::uint32_t* NativePointers::NetworkTime() const noexcept
    {
        return m_NetworkTime;
    }
}
