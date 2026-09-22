#pragma once

#include "../../game/native/NativeCallContext.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace TutonesV2::Features::Online
{
    struct OnlinePlayerEntry final
    {
        int id{-1};
        bool active{};
        bool local{};
        bool freemodeHost{};
        std::string name{};
        int ped{};
        bool healthReadable{};
        int health{};
        int maxHealth{};
        bool armourReadable{};
        int armour{};
        bool wantedReadable{};
        int wantedLevel{};
        bool positionReadable{};
        Game::Native::NativeVector3 position{};
        bool vehicleReadable{};
        int vehicle{};
        bool latencyReadable{};
        float latency{};
        bool packetLossReadable{};
        float packetLoss{};
        bool resendReadable{};
        int resendCount{};
    };

    struct OnlinePlayerRosterSnapshot final
    {
        bool ready{};
        bool refreshPending{};
        bool actionPending{};
        int localPlayer{-1};
        int freemodeHost{-1};
        int freemodeParticipants{-1};
        int activeCount{};
        std::array<OnlinePlayerEntry, 32> players{};
        std::uint64_t generation{};
        std::string message{"Ready"};
    };

    enum class OnlinePlayerAction : std::uint8_t
    {
        None,
        Spectate,
        StopSpectating,
        TeleportToPlayer,
        WaypointToPlayer,
    };

    class OnlinePlayerService final
    {
    public:
        static OnlinePlayerService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] OnlinePlayerRosterSnapshot Snapshot() const;

        void RequestRefresh() noexcept;
        bool QueueSpectate(int playerId) noexcept;
        bool QueueStopSpectating() noexcept;
        bool QueueTeleportToPlayer(int playerId) noexcept;
        bool QueueWaypointToPlayer(int playerId) noexcept;

    private:
        OnlinePlayerService() = default;

        void RefreshOnGameThread() noexcept;
        bool QueueAction(OnlinePlayerAction action, int playerId) noexcept;
        void ExecuteAction(OnlinePlayerAction action, int playerId) noexcept;
        [[nodiscard]] int ResolveTargetPed(int playerId) const noexcept;
        [[nodiscard]] int ResolveLocalTeleportEntity() const noexcept;
        void FinishAction(bool success, std::string message) noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_RefreshPending{};
        std::atomic_bool m_ActionPending{};
        std::atomic_uint64_t m_LastRefreshMs{};
        mutable std::mutex m_Mutex;
        OnlinePlayerRosterSnapshot m_Snapshot{};
    };
}
