#pragma once

#include "types/ScriptTypes.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

namespace TutonesV2::Game
{
    enum class NativeRuntimeState : std::uint8_t
    {
        Unavailable,
        PointersReady,
        SchedulerActive,
        HandlersReady,
        Ready,
    };

    class GameRuntime final
    {
    public:
        static GameRuntime& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool NativeRuntimeAvailable() const noexcept;
        [[nodiscard]] bool NativeReady() const noexcept;
        [[nodiscard]] bool NativeCanaryPassed() const noexcept;
        [[nodiscard]] bool IsOnGameThread() const noexcept;
        [[nodiscard]] NativeRuntimeState NativeState() const noexcept;
        [[nodiscard]] static const char* NativeStateName(NativeRuntimeState state) noexcept;

        void MarkSchedulerHookInstalled() noexcept;
        void OnScriptSchedulerTick() noexcept;
        bool Enqueue(std::function<void()> task);

    private:
        Types::ScriptThread* FindExecutionThread() const noexcept;
        void DrainTasks() noexcept;

        std::atomic_bool m_Initialized{};
        std::atomic_bool m_ShuttingDown{};
        std::atomic_bool m_NativeCanaryPassed{};
        std::atomic_uint32_t m_ActiveCallbacks{};
        std::atomic<NativeRuntimeState> m_NativeState{NativeRuntimeState::Unavailable};
        bool m_NativeInitAttempted{};
        bool m_NativeCanaryFailureLogged{};
        bool m_LoggedNoScriptThread{};
        bool m_LoggedNoTls{};

        std::mutex m_TaskMutex;
        std::deque<std::function<void()>> m_Tasks;
    };
}
