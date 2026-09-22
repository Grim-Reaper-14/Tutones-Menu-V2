#include "TeleportService.hpp"

#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace TutonesV2::Features::World
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;
        using NativeVector3 = Game::Native::NativeVector3;

        constexpr float MaxGroundCheck = 1000.0f;
        constexpr int MaxGroundAttempts = 20;

        // Current YimMenuV2 TpToObjective sprite order.
        constexpr std::array<int, 17> ObjectiveSprites{{
            1,   // RADAR_LEVEL
            0,   // RADAR_HIGHER
            2,   // RADAR_LOWER
            143, // RADAR_OBJECTIVE_BLUE
            144, // RADAR_OBJECTIVE_GREEN
            145, // RADAR_OBJECTIVE_RED
            146, // RADAR_OBJECTIVE_YELLOW
            478, // RADAR_CONTRABAND
            535, // RADAR_TARGET_A
            536, // RADAR_TARGET_B
            537, // RADAR_TARGET_C
            538, // RADAR_TARGET_D
            539, // RADAR_TARGET_E
            540, // RADAR_TARGET_F
            541, // RADAR_TARGET_G
            542, // RADAR_TARGET_H
            549, // RADAR_PICKUP_MACHINEGUN
        }};

        [[nodiscard]] bool EntityExists(int entity) noexcept
        {
            if (entity == 0)
                return false;

            const auto exists = NativeInvoker::Invoke<std::int32_t>(NativeId::DoesEntityExist, entity);
            return exists && *exists != 0;
        }
    }

    TeleportService& TeleportService::Get() noexcept
    {
        static TeleportService instance;
        return instance;
    }

    bool TeleportService::Initialize() noexcept
    {
        m_Pending.store(false, std::memory_order_release);
        m_AutoWaypoint.store(false, std::memory_order_release);
        m_AutoLoopQueued.store(false, std::memory_order_release);
        m_GroundZ = 0.0f;
        m_GroundAttempt = 0;
        m_FoundGround = false;
        m_ResolveCoords = {};
        m_ResolveLabel.clear();
        m_ControlEntity = 0;
        m_ControlAttempt = 0;
        m_ControlCoords = {};
        m_ControlLabel.clear();

        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot = {};
        }

        m_Ready.store(true, std::memory_order_release);
        return true;
    }

    void TeleportService::Shutdown() noexcept
    {
        m_Ready.store(false, std::memory_order_release);
        m_AutoWaypoint.store(false, std::memory_order_release);
        m_AutoLoopQueued.store(false, std::memory_order_release);
        m_Pending.store(false, std::memory_order_release);

        std::scoped_lock lock(m_Mutex);
        m_Snapshot.pending = false;
        m_Snapshot.autoWaypointEnabled = false;
        m_Snapshot.message = "Offline";
    }

    bool TeleportService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire);
    }

    TeleportSnapshot TeleportService::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        auto snapshot = m_Snapshot;
        snapshot.pending = m_Pending.load(std::memory_order_acquire);
        snapshot.autoWaypointEnabled = m_AutoWaypoint.load(std::memory_order_acquire);
        return snapshot;
    }

    bool TeleportService::AcquireAction(std::string message) noexcept
    {
        if (!IsReady() || !Game::GameRuntime::Get().NativeReady())
            return false;

        bool expected = false;
        if (!m_Pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return false;

        std::scoped_lock lock(m_Mutex);
        m_Snapshot.pending = true;
        m_Snapshot.lastSucceeded = false;
        m_Snapshot.message = std::move(message);
        return true;
    }

    int TeleportService::LocalPed() const noexcept
    {
        const auto ped = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
        return ped ? *ped : 0;
    }

    int TeleportService::TeleportEntity(int ped) const noexcept
    {
        if (ped == 0 || !EntityExists(ped))
            return 0;

        // Match YimMenuV2 Ped::TeleportTo: active vehicle first, otherwise the ped.
        const auto inVehicle = NativeInvoker::Invoke<std::int32_t>(
            NativeId::IsPedInAnyVehicle,
            ped,
            std::int32_t{0});

        if (inVehicle && *inVehicle != 0)
        {
            const auto vehicle = NativeInvoker::Invoke<std::int32_t>(
                NativeId::GetVehiclePedIsUsing,
                ped);

            if (vehicle && *vehicle != 0 && EntityExists(*vehicle))
                return *vehicle;
        }

        return ped;
    }

    bool TeleportService::QueueWaypoint() noexcept
    {
        if (!AcquireAction("Resolving waypoint..."))
            return false;

        if (Game::GameRuntime::Get().Enqueue([this] { ResolveWaypoint(false); }))
            return true;

        Finish(false, "Game-thread queue unavailable");
        return false;
    }

    bool TeleportService::QueueObjective() noexcept
    {
        if (!AcquireAction("Resolving objective..."))
            return false;

        if (Game::GameRuntime::Get().Enqueue([this] {
                for (const int sprite : ObjectiveSprites)
                {
                    const auto blip = NativeInvoker::Invoke<std::int32_t>(
                        NativeId::GetClosestBlipInfoId,
                        sprite);
                    if (!blip || *blip == 0)
                        continue;

                    const auto coords = NativeInvoker::Invoke<NativeVector3>(
                        NativeId::GetBlipCoords,
                        *blip);
                    if (!coords)
                        continue;

                    auto destination = *coords;
                    destination.z += 1.0f;
                    TeleportResolved(destination, "Objective teleport");
                    return;
                }

                Finish(false, "No supported objective blip is active");
            }))
        {
            return true;
        }

        Finish(false, "Game-thread queue unavailable");
        return false;
    }

    bool TeleportService::QueueCoordinates(
        float x,
        float y,
        float z,
        bool resolveGround) noexcept
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return false;

        if (!AcquireAction("Coordinate teleport queued"))
            return false;

        if (Game::GameRuntime::Get().Enqueue([this, x, y, z, resolveGround] {
                NativeVector3 coords{x, y, z};
                if (resolveGround)
                    BeginZResolution(coords, "Coordinate teleport");
                else
                    TeleportResolved(coords, "Coordinate teleport");
            }))
        {
            return true;
        }

        Finish(false, "Game-thread queue unavailable");
        return false;
    }

    bool TeleportService::QueueDirectional(
        float right,
        float forward,
        float up) noexcept
    {
        if (!std::isfinite(right) || !std::isfinite(forward) || !std::isfinite(up))
            return false;

        if (!AcquireAction("Directional teleport queued"))
            return false;

        if (Game::GameRuntime::Get().Enqueue([this, right, forward, up] {
                const int ped = LocalPed();
                if (ped == 0)
                {
                    Finish(false, "Local player ped is unavailable");
                    return;
                }

                const auto coords = NativeInvoker::Invoke<NativeVector3>(
                    NativeId::GetOffsetFromEntityInWorldCoords,
                    ped,
                    right,
                    forward,
                    up);

                if (!coords)
                {
                    Finish(false, "Directional offset could not be resolved");
                    return;
                }

                TeleportResolved(*coords, "Directional teleport");
            }))
        {
            return true;
        }

        Finish(false, "Game-thread queue unavailable");
        return false;
    }

    void TeleportService::SetAutoWaypoint(bool enabled) noexcept
    {
        m_AutoWaypoint.store(enabled, std::memory_order_release);

        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot.autoWaypointEnabled = enabled;
            if (!m_Pending.load(std::memory_order_acquire))
                m_Snapshot.message = enabled
                    ? "Auto waypoint teleport enabled"
                    : "Auto waypoint teleport disabled";
        }

        if (enabled)
            EnsureAutoLoop();
    }

    void TeleportService::ResolveWaypoint(bool automatic) noexcept
    {
        const auto active = NativeInvoker::Invoke<std::int32_t>(NativeId::IsWaypointActive);
        if (!active || *active == 0)
        {
            Finish(false, automatic ? "Auto waypoint disappeared" : "Set a waypoint first");
            return;
        }

        const auto waypointEnum = NativeInvoker::Invoke<std::int32_t>(
            NativeId::GetWaypointBlipEnumId);
        if (!waypointEnum || *waypointEnum == 0)
        {
            Finish(false, "Waypoint blip type is unavailable");
            return;
        }

        const auto blip = NativeInvoker::Invoke<std::int32_t>(
            NativeId::GetClosestBlipInfoId,
            *waypointEnum);
        if (!blip || *blip == 0)
        {
            Finish(false, "Waypoint blip is unavailable");
            return;
        }

        const auto coords = NativeInvoker::Invoke<NativeVector3>(
            NativeId::GetBlipCoords,
            *blip);
        if (!coords)
        {
            Finish(false, "Waypoint coordinates are unavailable");
            return;
        }

        if (automatic)
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetWaypointOff));

        BeginZResolution(
            *coords,
            automatic ? "Auto waypoint teleport" : "Waypoint teleport");
    }

    void TeleportService::BeginZResolution(
        NativeVector3 coords,
        std::string label) noexcept
    {
        // Mirrors YimMenuV2 ResolveZCoordinate: collision request + up to 20
        // scheduler yields, then water and approximate-height fallbacks.
        m_ResolveCoords = coords;
        m_GroundZ = coords.z;
        m_GroundAttempt = 0;
        m_FoundGround = false;
        m_ResolveLabel = std::move(label);

        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot.message = m_ResolveLabel + ": resolving ground";
        }

        ResolveZTick();
    }

    void TeleportService::ResolveZTick() noexcept
    {
        if (!m_Pending.load(std::memory_order_acquire))
            return;

        static_cast<void>(NativeInvoker::InvokeVoid(
            NativeId::RequestCollisionAtCoord,
            m_ResolveCoords.x,
            m_ResolveCoords.y,
            m_ResolveCoords.z));

        float groundZ = m_GroundZ;
        const auto groundFound = NativeInvoker::Invoke<std::int32_t>(
            NativeId::GetGroundZFor3DCoord,
            m_ResolveCoords.x,
            m_ResolveCoords.y,
            MaxGroundCheck,
            &groundZ,
            std::int32_t{0},
            std::int32_t{0});

        if (groundFound && *groundFound != 0 && std::isfinite(groundZ))
        {
            m_GroundZ = groundZ;
            m_ResolveCoords.z = groundZ + 1.0f;
            m_FoundGround = true;
            FinishZResolution();
            return;
        }

        if ((m_GroundAttempt % 3) == 0)
            m_GroundZ += 25.0f;

        ++m_GroundAttempt;
        if (m_GroundAttempt < MaxGroundAttempts)
        {
            if (Game::GameRuntime::Get().Enqueue([this] { ResolveZTick(); }))
                return;

            Finish(false, "Ground-resolution queue unavailable");
            return;
        }

        FinishZResolution();
    }

    void TeleportService::FinishZResolution() noexcept
    {
        float waterHeight{};
        const auto waterFound = NativeInvoker::Invoke<std::int32_t>(
            NativeId::GetWaterHeight,
            m_ResolveCoords.x,
            m_ResolveCoords.y,
            m_ResolveCoords.z,
            &waterHeight);

        if (waterFound && *waterFound != 0 && std::isfinite(waterHeight))
        {
            m_ResolveCoords.z = waterHeight;
        }
        else if (!m_FoundGround)
        {
            const auto approximateHeight = NativeInvoker::Invoke<float>(
                NativeId::GetApproxHeightForPoint,
                m_ResolveCoords.x,
                m_ResolveCoords.y);

            if (approximateHeight && std::isfinite(*approximateHeight))
                m_ResolveCoords.z = *approximateHeight;
        }

        TeleportResolved(m_ResolveCoords, m_ResolveLabel);
    }

    void TeleportService::TeleportResolved(
        const NativeVector3& coords,
        std::string label) noexcept
    {
        if (!std::isfinite(coords.x)
            || !std::isfinite(coords.y)
            || !std::isfinite(coords.z))
        {
            Finish(false, std::move(label) + " failed: invalid coordinates");
            return;
        }

        const int ped = LocalPed();
        if (ped == 0 || !EntityExists(ped))
        {
            Finish(false, std::move(label) + " failed: local player unavailable");
            return;
        }

        // YimMenuV2 Ped::TeleportTo uses the active vehicle returned by
        // GET_VEHICLE_PED_IS_USING; otherwise it moves the ped directly.
        const int entity = TeleportEntity(ped);
        if (entity == 0)
        {
            Finish(false, std::move(label) + " failed: local player/vehicle unavailable");
            return;
        }

        if (entity != ped)
        {
            BeginControlledVehicleTeleport(entity, coords, std::move(label));
            return;
        }

        MoveResolvedEntity(entity, coords, std::move(label));
    }

    void TeleportService::BeginControlledVehicleTeleport(
        int vehicle,
        const NativeVector3& coords,
        std::string label) noexcept
    {
        m_ControlEntity = vehicle;
        m_ControlCoords = coords;
        m_ControlLabel = std::move(label);
        m_ControlAttempt = 0;

        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot.message = m_ControlLabel + ": acquiring active-vehicle control";
        }

        ControlledVehicleTeleportTick();
    }

    void TeleportService::ControlledVehicleTeleportTick() noexcept
    {
        if (!m_Pending.load(std::memory_order_acquire))
            return;

        if (!EntityExists(m_ControlEntity))
        {
            Finish(false, m_ControlLabel + " failed: active vehicle disappeared");
            return;
        }

        const auto hasControl = NativeInvoker::Invoke<std::int32_t>(
            NativeId::NetworkHasControlOfEntity,
            m_ControlEntity);

        if (hasControl && *hasControl != 0)
        {
            MoveResolvedEntity(m_ControlEntity, m_ControlCoords, m_ControlLabel);
            return;
        }

        static_cast<void>(NativeInvoker::Invoke<std::int32_t>(
            NativeId::NetworkRequestControlOfEntity,
            m_ControlEntity));

        ++m_ControlAttempt;
        if (m_ControlAttempt < 10)
        {
            if (Game::GameRuntime::Get().Enqueue([this] { ControlledVehicleTeleportTick(); }))
                return;
        }

        // YimMenuV2 ultimately calls Entity::SetPosition even if its debug
        // control assertion only warns. Preserve that final behavior here.
        MoveResolvedEntity(m_ControlEntity, m_ControlCoords, m_ControlLabel);
    }

    void TeleportService::MoveResolvedEntity(
        int entity,
        const NativeVector3& coords,
        std::string label) noexcept
    {
        const bool moved = NativeInvoker::InvokeVoid(
            NativeId::SetEntityCoordsNoOffset,
            entity,
            coords.x,
            coords.y,
            coords.z,
            std::int32_t{1},
            std::int32_t{1},
            std::int32_t{1});

        Finish(
            moved,
            std::move(label) + (moved ? " complete" : " failed"));
    }

    void TeleportService::EnsureAutoLoop() noexcept
    {
        if (!IsReady() || !m_AutoWaypoint.load(std::memory_order_acquire))
            return;

        bool expected = false;
        if (!m_AutoLoopQueued.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel))
        {
            return;
        }

        if (!Game::GameRuntime::Get().Enqueue([this] { AutoTick(); }))
            m_AutoLoopQueued.store(false, std::memory_order_release);
    }

    void TeleportService::AutoTick() noexcept
    {
        if (!IsReady() || !m_AutoWaypoint.load(std::memory_order_acquire))
        {
            m_AutoLoopQueued.store(false, std::memory_order_release);
            return;
        }

        if (!m_Pending.load(std::memory_order_acquire))
        {
            const auto active = NativeInvoker::Invoke<std::int32_t>(NativeId::IsWaypointActive);
            if (active && *active != 0)
            {
                bool expected = false;
                if (m_Pending.compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_acq_rel))
                {
                    {
                        std::scoped_lock lock(m_Mutex);
                        m_Snapshot.pending = true;
                        m_Snapshot.lastSucceeded = false;
                        m_Snapshot.message = "Auto waypoint detected";
                    }

                    ResolveWaypoint(true);
                }
            }
        }

        if (IsReady()
            && m_AutoWaypoint.load(std::memory_order_acquire)
            && Game::GameRuntime::Get().Enqueue([this] { AutoTick(); }))
        {
            return;
        }

        m_AutoLoopQueued.store(false, std::memory_order_release);
    }

    void TeleportService::Finish(bool success, std::string message) noexcept
    {
        m_Pending.store(false, std::memory_order_release);
        m_GroundAttempt = 0;
        m_FoundGround = false;
        m_ResolveLabel.clear();
        m_ControlEntity = 0;
        m_ControlAttempt = 0;
        m_ControlLabel.clear();

        std::scoped_lock lock(m_Mutex);
        m_Snapshot.pending = false;
        m_Snapshot.lastSucceeded = success;
        m_Snapshot.message = std::move(message);
    }
}
