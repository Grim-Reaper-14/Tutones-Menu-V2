#pragma once

#include "../../game/native/NativeCallContext.hpp"

#include <atomic>
#include <mutex>
#include <string>

namespace TutonesV2::Features::World
{
    struct TeleportSnapshot final
    {
        bool pending{};
        bool autoWaypointEnabled{};
        bool lastSucceeded{};
        std::string message{"Ready"};
    };

    class TeleportService final
    {
    public:
        static TeleportService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] TeleportSnapshot Snapshot() const;

        bool QueueWaypoint() noexcept;
        bool QueueObjective() noexcept;
        bool QueueCoordinates(float x, float y, float z, bool resolveGround) noexcept;
        bool QueueDirectional(float right, float forward, float up) noexcept;
        void SetAutoWaypoint(bool enabled) noexcept;

    private:
        TeleportService() = default;

        bool AcquireAction(std::string message) noexcept;
        void ResolveWaypoint(bool automatic) noexcept;
        void BeginZResolution(Game::Native::NativeVector3 coords, std::string label) noexcept;
        void ResolveZTick() noexcept;
        void FinishZResolution() noexcept;
        void TeleportResolved(const Game::Native::NativeVector3& coords, std::string label) noexcept;
        [[nodiscard]] int LocalPed() const noexcept;
        [[nodiscard]] int TeleportEntity(int ped) const noexcept;

        void EnsureAutoLoop() noexcept;
        void AutoTick() noexcept;

        void Finish(bool success, std::string message) noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_Pending{};
        std::atomic_bool m_AutoWaypoint{};
        std::atomic_bool m_AutoLoopQueued{};

        Game::Native::NativeVector3 m_ResolveCoords{};
        float m_GroundZ{};
        int m_GroundAttempt{};
        bool m_FoundGround{};
        std::string m_ResolveLabel{};

        mutable std::mutex m_Mutex;
        TeleportSnapshot m_Snapshot{};
    };
}
