#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace TutonesV2::Features::Player
{
    enum class RadarMode : int
    {
        Off = 0,
        OffRadar = 1,
        GhostOrganization = 2
    };

    struct SelfOnlineSnapshot final
    {
        RadarMode mode{RadarMode::Off};
        bool sessionStarted{};
        bool freemodeReady{};
        bool globalsReady{};
        bool safeToModify{};
        bool offRadarApplied{};
        bool ghostOrganizationApplied{};
        std::string message{"Ready"};
    };

    class SelfOnlineService final
    {
    public:
        static SelfOnlineService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] RadarMode Mode() const noexcept;
        [[nodiscard]] SelfOnlineSnapshot Snapshot() const;

        bool SetMode(RadarMode mode) noexcept;

    private:
        SelfOnlineService() = default;

        bool EnsureLoop() noexcept;
        void Tick() noexcept;
        void Apply(RadarMode mode) noexcept;
        [[nodiscard]] bool FreemodeThreadReady() const noexcept;
        void Publish(SelfOnlineSnapshot snapshot);

        std::atomic_bool m_Ready{};
        std::atomic_bool m_LoopQueued{};
        std::atomic_bool m_OffRadarApplied{};
        std::atomic_bool m_GhostApplied{};
        std::atomic<RadarMode> m_Mode{RadarMode::Off};
        mutable std::mutex m_Mutex;
        SelfOnlineSnapshot m_Snapshot{};
    };
}
