#pragma once

#include "types/ScriptTypes.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

namespace TutonesV2::Game
{
    class GameRuntime final
    {
    public:
        static GameRuntime& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool NativeReady() const noexcept;
        [[nodiscard]] bool NativeCanaryPassed() const noexcept;

        void OnScriptSchedulerTick() noexcept;
        bool Enqueue(std::function<void()> task);

    private:
        Types::ScriptThread* FindExecutionThread() const noexcept;
        void DrainTasks() noexcept;

        std::atomic_bool m_Initialized{};
        std::atomic_bool m_ShuttingDown{};
        std::atomic_bool m_NativeCanaryPassed{};
        std::atomic_uint32_t m_ActiveCallbacks{};
        bool m_NativeInitAttempted{};
        bool m_NativeCanaryFailureLogged{};
        bool m_LoggedNoScriptThread{};
        bool m_LoggedNoTls{};

        std::mutex m_TaskMutex;
        std::deque<std::function<void()>> m_Tasks;
    };
}
