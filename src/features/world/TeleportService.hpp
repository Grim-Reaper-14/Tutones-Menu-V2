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
        bool QueueCoordinates(float x,float y,float z,bool resolveGround) noexcept;

    private:
        TeleportService()=default;
        struct Target { int entity{}; int vehicle{}; bool inVehicle{}; };
        bool BeginTeleport(Game::Native::NativeVector3 coords,bool resolveGround,std::string label) noexcept;
        void GroundTick() noexcept;
        void SettleTick() noexcept;
        void Finish(bool success,std::string message) noexcept;
        void Restore(std::string message) noexcept;
        Target ResolveTarget() noexcept;
        bool Move(const Game::Native::NativeVector3& coords) noexcept;
        bool Freeze(bool enabled) noexcept;
        void StreamCollision(const Game::Native::NativeVector3& coords) noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_Pending{};
        Target m_Target{};
        Game::Native::NativeVector3 m_Original{};
        Game::Native::NativeVector3 m_Destination{};
        int m_Attempts{};
        bool m_ResolveGround{};
        std::string m_Label{};
        mutable std::mutex m_Mutex;
        TeleportSnapshot m_Snapshot{};
    };
}
