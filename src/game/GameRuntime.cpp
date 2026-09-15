#include "GameRuntime.hpp"

#include "GameProcess.hpp"
#include "native/NativeInvoker.hpp"
#include "native/NativePointers.hpp"
#include "native/NativeRegistry.hpp"
#include "../core/Logger.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <thread>

namespace TutonesV2::Game
{
    namespace
    {
        constexpr std::uint32_t Joaat(const char* text) noexcept
        {
            std::uint32_t hash{};
            while (text && *text)
            {
                char c = *text++;
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');

                hash += static_cast<std::uint8_t>(c);
                hash += hash << 10;
                hash ^= hash >> 6;
            }

            hash += hash << 3;
            hash ^= hash >> 11;
            hash += hash << 15;
            return hash;
        }

        constexpr std::array<std::uint32_t, 3> PreferredScriptHashes{
            Joaat("freemode"),
            Joaat("main_persistent"),
            Joaat("startup"),
        };

        constexpr std::size_t MaxQueuedTasks = 256;
        constexpr std::size_t MaxTasksPerTick = 32;

        class ScriptTlsScope final
        {
        public:
            ScriptTlsScope(Types::TlsContext* tls, Types::ScriptThread* thread) noexcept
                : m_Tls(tls)
            {
                if (!m_Tls || !thread)
                    return;

                m_PreviousThread = m_Tls->currentScriptThread;
                m_PreviousActive = m_Tls->scriptThreadActive;
                m_Tls->currentScriptThread = thread;
                m_Tls->scriptThreadActive = true;
                m_Active = true;
            }

            ~ScriptTlsScope()
            {
                if (!m_Active || !m_Tls)
                    return;

                m_Tls->scriptThreadActive = m_PreviousActive;
                m_Tls->currentScriptThread = m_PreviousThread;
            }

            [[nodiscard]] bool IsActive() const noexcept
            {
                return m_Active;
            }

        private:
            Types::TlsContext* m_Tls{};
            Types::ScriptThread* m_PreviousThread{};
            bool m_PreviousActive{};
            bool m_Active{};
        };

        class CallbackScope final
        {
        public:
            explicit CallbackScope(std::atomic_uint32_t& counter) noexcept
                : m_Counter(counter)
            {
                m_Counter.fetch_add(1, std::memory_order_acq_rel);
            }

            ~CallbackScope()
            {
                m_Counter.fetch_sub(1, std::memory_order_acq_rel);
            }

        private:
            std::atomic_uint32_t& m_Counter;
        };
    }

    GameRuntime& GameRuntime::Get() noexcept
    {
        static GameRuntime instance;
        return instance;
    }

    bool GameRuntime::Initialize() noexcept
    {
        bool expected = false;
        if (!m_Initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return true;

        m_ShuttingDown.store(false, std::memory_order_release);
        m_NativeCanaryPassed.store(false, std::memory_order_release);
        m_ActiveCallbacks.store(0, std::memory_order_release);
        m_NativeState.store(NativeRuntimeState::Unavailable, std::memory_order_release);
        m_NativeInitAttempted = false;
        m_NativeCanaryFailureLogged = false;
        m_LoggedNoScriptThread = false;
        m_LoggedNoTls = false;

        {
            std::scoped_lock lock(m_TaskMutex);
            m_Tasks.clear();
        }

        if (!GameProcess::IsEnhancedHost())
        {
            Core::Logger::Get().Error("game", "Tutones Menu V2 must be loaded inside GTA5_Enhanced.exe");
            m_Initialized.store(false, std::memory_order_release);
            return false;
        }

        if (!Native::NativePointers::Get().Resolve())
        {
            Core::Logger::Get().Warn(
                "game",
                "Native compatibility gate blocked the GTA scheduler runtime; V2 will continue in DX12/UI-only mode");
            return true;
        }

        m_NativeState.store(NativeRuntimeState::PointersReady, std::memory_order_release);
        Core::Logger::Get().Info(
            "game",
            "GTA5_Enhanced.exe detected; native pointers resolved and scheduler hook is eligible to install");
        return true;
    }

    void GameRuntime::Shutdown() noexcept
    {
        if (!m_Initialized.exchange(false, std::memory_order_acq_rel))
            return;

        m_ShuttingDown.store(true, std::memory_order_release);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (m_ActiveCallbacks.load(std::memory_order_acquire) != 0
            && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (m_ActiveCallbacks.load(std::memory_order_acquire) != 0)
            Core::Logger::Get().Warn("game", "Timed out waiting for native scheduler callbacks to drain");

        {
            std::scoped_lock lock(m_TaskMutex);
            m_Tasks.clear();
        }

        Native::NativeRegistry::Get().Shutdown();
        Native::NativePointers::Get().Reset();

        m_NativeCanaryPassed.store(false, std::memory_order_release);
        m_NativeState.store(NativeRuntimeState::Unavailable, std::memory_order_release);
        m_NativeInitAttempted = false;
        m_NativeCanaryFailureLogged = false;
        m_LoggedNoScriptThread = false;
        m_LoggedNoTls = false;
        m_ShuttingDown.store(false, std::memory_order_release);
        Core::Logger::Get().Info("game", "GTA native runtime stopped");
    }

    bool GameRuntime::IsInitialized() const noexcept
    {
        return m_Initialized.load(std::memory_order_acquire);
    }

    bool GameRuntime::NativeRuntimeAvailable() const noexcept
    {
        return NativeState() != NativeRuntimeState::Unavailable;
    }

    bool GameRuntime::NativeReady() const noexcept
    {
        return NativeState() == NativeRuntimeState::Ready
            && Native::NativeRegistry::Get().IsReady()
            && m_NativeCanaryPassed.load(std::memory_order_acquire);
    }

    bool GameRuntime::NativeCanaryPassed() const noexcept
    {
        return m_NativeCanaryPassed.load(std::memory_order_acquire);
    }

    NativeRuntimeState GameRuntime::NativeState() const noexcept
    {
        return m_NativeState.load(std::memory_order_acquire);
    }

    const char* GameRuntime::NativeStateName(NativeRuntimeState state) noexcept
    {
        switch (state)
        {
        case NativeRuntimeState::Unavailable: return "Unavailable / compatibility gate blocked";
        case NativeRuntimeState::PointersReady: return "Pointers ready / scheduler hook pending";
        case NativeRuntimeState::SchedulerActive: return "Scheduler active / native table pending";
        case NativeRuntimeState::HandlersReady: return "Handlers cached / canary pending";
        case NativeRuntimeState::Ready: return "Ready";
        }
        return "Unknown";
    }

    void GameRuntime::MarkSchedulerHookInstalled() noexcept
    {
        NativeRuntimeState expected = NativeRuntimeState::PointersReady;
        if (m_NativeState.compare_exchange_strong(
                expected,
                NativeRuntimeState::SchedulerActive,
                std::memory_order_acq_rel))
        {
            Core::Logger::Get().Info("game", "Native compatibility gate opened GTA scheduler execution");
        }
    }

    bool GameRuntime::Enqueue(std::function<void()> task)
    {
        if (!task || !IsInitialized() || !NativeReady()
            || m_ShuttingDown.load(std::memory_order_acquire))
        {
            return false;
        }

        std::scoped_lock lock(m_TaskMutex);
        if (m_Tasks.size() >= MaxQueuedTasks)
        {
            Core::Logger::Get().Warn("game", "Native game-thread task queue is full; task rejected");
            return false;
        }

        m_Tasks.emplace_back(std::move(task));
        return true;
    }

    void GameRuntime::OnScriptSchedulerTick() noexcept
    {
        if (!IsInitialized() || !NativeRuntimeAvailable()
            || m_ShuttingDown.load(std::memory_order_acquire))
        {
            return;
        }

        CallbackScope callback(m_ActiveCallbacks);

        auto* scriptThread = FindExecutionThread();
        if (!scriptThread)
        {
            if (!m_LoggedNoScriptThread)
            {
                m_LoggedNoScriptThread = true;
                Core::Logger::Get().Warn(
                    "game",
                    "No freemode/main_persistent/startup script thread is available yet");
            }
            return;
        }
        m_LoggedNoScriptThread = false;

        auto* tls = Types::TlsContext::Get();
        if (!tls)
        {
            if (!m_LoggedNoTls)
            {
                m_LoggedNoTls = true;
                Core::Logger::Get().Error("game", "GTA TLS context is unavailable on the script scheduler thread");
            }
            return;
        }
        m_LoggedNoTls = false;

        ScriptTlsScope scope(tls, scriptThread);
        if (!scope.IsActive())
            return;

        auto& registry = Native::NativeRegistry::Get();
        registry.MarkGameThread(::GetCurrentThreadId());

        if (!registry.IsReady())
        {
            if (m_NativeInitAttempted)
                return;

            m_NativeInitAttempted = true;
            Core::Logger::Get().Info("game", "Initializing V1 native catalog inside GTA script TLS scope");
            if (!registry.Initialize(Native::NativePointers::Get().InitNativeTables()))
            {
                m_NativeState.store(NativeRuntimeState::Unavailable, std::memory_order_release);
                Core::Logger::Get().Error(
                    "game",
                    "Native compatibility gate closed: V2 native handler table failed to initialize");
                return;
            }

            m_NativeState.store(NativeRuntimeState::HandlersReady, std::memory_order_release);
            Core::Logger::Get().Info("game", "Native handlers cached; canary deferred to next scheduler tick");
            return;
        }

        if (!m_NativeCanaryPassed.load(std::memory_order_acquire))
        {
            const auto ped = Native::NativeInvoker::Invoke<std::int32_t>(Native::NativeId::PlayerPedId);
            if (!ped)
            {
                m_NativeState.store(NativeRuntimeState::Unavailable, std::memory_order_release);
                if (!m_NativeCanaryFailureLogged)
                {
                    m_NativeCanaryFailureLogged = true;
                    Core::Logger::Get().Error(
                        "game",
                        "Native compatibility gate closed: PLAYER_PED_ID canary invocation was rejected");
                }
                return;
            }

            m_NativeCanaryPassed.store(true, std::memory_order_release);
            m_NativeState.store(NativeRuntimeState::Ready, std::memory_order_release);
            Core::Logger::Get().Info(
                "game",
                std::string("Native canary passed: PLAYER_PED_ID returned ") + std::to_string(*ped));
            return;
        }

        DrainTasks();
    }

    void GameRuntime::DrainTasks() noexcept
    {
        std::size_t taskBudget{};
        {
            std::scoped_lock lock(m_TaskMutex);
            taskBudget = std::min(m_Tasks.size(), MaxTasksPerTick);
        }

        for (std::size_t taskIndex = 0; taskIndex < taskBudget; ++taskIndex)
        {
            std::function<void()> task;
            {
                std::scoped_lock lock(m_TaskMutex);
                if (m_Tasks.empty())
                    break;
                task = std::move(m_Tasks.front());
                m_Tasks.pop_front();
            }

            try
            {
                task();
            }
            catch (const std::exception& exception)
            {
                Core::Logger::Get().Error(
                    "game",
                    std::string("Native task threw exception: ") + exception.what());
            }
            catch (...)
            {
                Core::Logger::Get().Error("game", "Native task threw an unknown exception");
            }
        }
    }

    Types::ScriptThread* GameRuntime::FindExecutionThread() const noexcept
    {
        const auto* threads = Native::NativePointers::Get().ScriptThreads();
        if (!threads || !threads->data || threads->size == 0 || threads->size > threads->capacity)
            return nullptr;

        for (const auto preferredHash : PreferredScriptHashes)
        {
            for (std::uint16_t index = 0; index < threads->size; ++index)
            {
                auto* thread = threads->data[index];
                if (!thread || thread->context.threadId == 0)
                    continue;
                if (thread->scriptHash == preferredHash)
                    return thread;
            }
        }

        return nullptr;
    }
}
