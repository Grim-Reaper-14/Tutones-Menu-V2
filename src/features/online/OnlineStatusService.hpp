#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace TutonesV2::Features::Online
{
    struct OnlineStatusSnapshot final
    {
        bool ready{};
        bool refreshPending{};
        bool sessionStarted{};
        bool globalsReady{};
        bool networkTimeReady{};
        bool freemodeReady{};
        int localPlayer{-1};
        int scriptThreadCount{};
        int activeScriptThreads{};
        std::uint32_t networkTime{};
        std::string message{"Ready"};
    };

    class OnlineStatusService final
    {
    public:
        static OnlineStatusService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] OnlineStatusSnapshot Snapshot() const;

        void RequestRefresh() noexcept;

    private:
        OnlineStatusService() = default;
        void RefreshOnGameThread() noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_RefreshPending{};
        std::atomic_uint64_t m_LastRefreshMs{};
        mutable std::mutex m_Mutex;
        OnlineStatusSnapshot m_Snapshot{};
    };
}
