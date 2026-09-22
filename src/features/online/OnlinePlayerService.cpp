#include "OnlinePlayerService.hpp"

#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"
#include "../../game/native/NativePointers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string_view>
#include <utility>

namespace TutonesV2::Features::Online
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;

        std::uint64_t NowMs() noexcept
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(
                duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        }

        bool EntityExists(int entity) noexcept
        {
            if (entity == 0)
                return false;
            const auto exists = NativeInvoker::Invoke<std::int32_t>(
                NativeId::DoesEntityExist,
                entity);
            return exists && *exists != 0;
        }
    }

    OnlinePlayerService& OnlinePlayerService::Get() noexcept
    {
        static OnlinePlayerService instance;
        return instance;
    }

    bool OnlinePlayerService::Initialize() noexcept
    {
        m_RefreshPending.store(false, std::memory_order_release);
        m_ActionPending.store(false, std::memory_order_release);
        m_LastRefreshMs.store(0, std::memory_order_release);
        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot = {};
            m_Snapshot.ready = true;
        }
        m_Ready.store(true, std::memory_order_release);
        return true;
    }

    void OnlinePlayerService::Shutdown() noexcept
    {
        m_Ready.store(false, std::memory_order_release);
        m_RefreshPending.store(false, std::memory_order_release);
        m_ActionPending.store(false, std::memory_order_release);
    }

    bool OnlinePlayerService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire);
    }

    OnlinePlayerRosterSnapshot OnlinePlayerService::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        auto out = m_Snapshot;
        out.ready = IsReady();
        out.refreshPending = m_RefreshPending.load(std::memory_order_acquire);
        out.actionPending = m_ActionPending.load(std::memory_order_acquire);
        return out;
    }

    void OnlinePlayerService::RequestRefresh() noexcept
    {
        if (!IsReady() || !Game::GameRuntime::Get().NativeReady())
            return;

        const auto now = NowMs();
        const auto last = m_LastRefreshMs.load(std::memory_order_acquire);
        if (last != 0 && now - last < 750)
            return;

        bool expected = false;
        if (!m_RefreshPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        m_LastRefreshMs.store(now, std::memory_order_release);
        if (!Game::GameRuntime::Get().Enqueue([this] { RefreshOnGameThread(); }))
            m_RefreshPending.store(false, std::memory_order_release);
    }

    void OnlinePlayerService::RefreshOnGameThread() noexcept
    {
        OnlinePlayerRosterSnapshot next{};
        next.ready = true;

        bool* session = Game::Native::NativePointers::Get().IsSessionStarted();
        if (!session || !*session)
        {
            next.message = "Join GTA Online to populate the player roster";
            {
                std::scoped_lock lock(m_Mutex);
                next.generation = m_Snapshot.generation + 1;
                m_Snapshot = std::move(next);
            }
            m_RefreshPending.store(false, std::memory_order_release);
            return;
        }

        if (const auto local = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerId);
            local && *local >= 0 && *local < 32)
        {
            next.localPlayer = *local;
        }

        if (const auto host = NativeInvoker::Invoke<std::int32_t>(
                NativeId::NetworkGetHostOfScript,
                "freemode",
                -1,
                0);
            host && *host >= 0 && *host < 32)
        {
            next.freemodeHost = *host;
        }

        if (const auto participants = NativeInvoker::Invoke<std::int32_t>(
                NativeId::NetworkGetNumScriptParticipants,
                "freemode",
                -1,
                0);
            participants && *participants >= 0 && *participants <= 32)
        {
            next.freemodeParticipants = *participants;
        }

        for (int playerId = 0; playerId < 32; ++playerId)
        {
            const auto active = NativeInvoker::Invoke<std::int32_t>(
                NativeId::NetworkIsPlayerActive,
                playerId);
            if (!active || *active == 0)
                continue;

            auto& player = next.players[static_cast<std::size_t>(playerId)];
            player.id = playerId;
            player.active = true;
            player.local = playerId == next.localPlayer;
            player.freemodeHost = playerId == next.freemodeHost;
            ++next.activeCount;

            if (const auto name = NativeInvoker::Invoke<const char*>(
                    NativeId::GetPlayerName,
                    playerId);
                name && *name && **name)
            {
                player.name = *name;
            }
            else
            {
                player.name = "Player " + std::to_string(playerId);
            }

            if (const auto ped = NativeInvoker::Invoke<std::int32_t>(
                    NativeId::GetPlayerPedScriptIndex,
                    playerId);
                ped && *ped != 0 && EntityExists(*ped))
            {
                player.ped = *ped;

                if (const auto health = NativeInvoker::Invoke<std::int32_t>(
                        NativeId::GetEntityHealth,
                        *ped))
                {
                    player.healthReadable = true;
                    player.health = *health;
                }

                if (const auto maxHealth = NativeInvoker::Invoke<std::int32_t>(
                        NativeId::GetEntityMaxHealth,
                        *ped))
                {
                    player.maxHealth = *maxHealth;
                }

                if (const auto armour = NativeInvoker::Invoke<std::int32_t>(
                        NativeId::GetPedArmour,
                        *ped))
                {
                    player.armourReadable = true;
                    player.armour = *armour;
                }

                if (const auto coords = NativeInvoker::Invoke<Game::Native::NativeVector3>(
                        NativeId::GetEntityCoords,
                        *ped,
                        std::int32_t{0}))
                {
                    player.positionReadable = true;
                    player.position = *coords;
                }

                const auto inVehicle = NativeInvoker::Invoke<std::int32_t>(
                    NativeId::IsPedInAnyVehicle,
                    *ped,
                    std::int32_t{0});
                if (inVehicle && *inVehicle != 0)
                {
                    if (const auto vehicle = NativeInvoker::Invoke<std::int32_t>(
                            NativeId::GetVehiclePedIsUsing,
                            *ped);
                        vehicle && *vehicle != 0 && EntityExists(*vehicle))
                    {
                        player.vehicleReadable = true;
                        player.vehicle = *vehicle;
                    }
                }
            }

            if (const auto wanted = NativeInvoker::Invoke<std::int32_t>(
                    NativeId::GetPlayerWantedLevel,
                    playerId))
            {
                player.wantedReadable = true;
                player.wantedLevel = *wanted;
            }

            if (const auto latency = NativeInvoker::Invoke<float>(
                    NativeId::NetworkGetAverageLatency,
                    playerId))
            {
                player.latencyReadable = std::isfinite(*latency);
                player.latency = *latency;
            }

            if (const auto loss = NativeInvoker::Invoke<float>(
                    NativeId::NetworkGetAveragePacketLoss,
                    playerId))
            {
                player.packetLossReadable = std::isfinite(*loss);
                player.packetLoss = *loss;
            }

            if (const auto resend = NativeInvoker::Invoke<std::int32_t>(
                    NativeId::NetworkGetHighestReliableResendCount,
                    playerId))
            {
                player.resendReadable = true;
                player.resendCount = *resend;
            }
        }

        next.message = "Online player roster refreshed";
        {
            std::scoped_lock lock(m_Mutex);
            next.generation = m_Snapshot.generation + 1;
            m_Snapshot = std::move(next);
        }

        m_RefreshPending.store(false, std::memory_order_release);
    }

    int OnlinePlayerService::ResolveTargetPed(int playerId) const noexcept
    {
        if (playerId < 0 || playerId >= 32)
            return 0;

        const auto active = NativeInvoker::Invoke<std::int32_t>(
            NativeId::NetworkIsPlayerActive,
            playerId);
        if (!active || *active == 0)
            return 0;

        const auto ped = NativeInvoker::Invoke<std::int32_t>(
            NativeId::GetPlayerPedScriptIndex,
            playerId);
        if (!ped || *ped == 0 || !EntityExists(*ped))
            return 0;
        return *ped;
    }

    int OnlinePlayerService::ResolveLocalTeleportEntity() const noexcept
    {
        const auto ped = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
        if (!ped || *ped == 0 || !EntityExists(*ped))
            return 0;

        const auto inVehicle = NativeInvoker::Invoke<std::int32_t>(
            NativeId::IsPedInAnyVehicle,
            *ped,
            std::int32_t{0});
        if (inVehicle && *inVehicle != 0)
        {
            const auto vehicle = NativeInvoker::Invoke<std::int32_t>(
                NativeId::GetVehiclePedIsUsing,
                *ped);
            if (vehicle && *vehicle != 0 && EntityExists(*vehicle))
                return *vehicle;
        }

        return *ped;
    }

    bool OnlinePlayerService::QueueAction(
        OnlinePlayerAction action,
        int playerId) noexcept
    {
        if (!IsReady() || !Game::GameRuntime::Get().NativeReady())
            return false;

        bool expected = false;
        if (!m_ActionPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return false;

        if (Game::GameRuntime::Get().Enqueue([this, action, playerId] {
                ExecuteAction(action, playerId);
            }))
        {
            return true;
        }

        m_ActionPending.store(false, std::memory_order_release);
        return false;
    }

    bool OnlinePlayerService::QueueSpectate(int playerId) noexcept
    {
        return QueueAction(OnlinePlayerAction::Spectate, playerId);
    }

    bool OnlinePlayerService::QueueStopSpectating() noexcept
    {
        return QueueAction(OnlinePlayerAction::StopSpectating, -1);
    }

    bool OnlinePlayerService::QueueTeleportToPlayer(int playerId) noexcept
    {
        return QueueAction(OnlinePlayerAction::TeleportToPlayer, playerId);
    }

    bool OnlinePlayerService::QueueWaypointToPlayer(int playerId) noexcept
    {
        return QueueAction(OnlinePlayerAction::WaypointToPlayer, playerId);
    }

    void OnlinePlayerService::ExecuteAction(
        OnlinePlayerAction action,
        int playerId) noexcept
    {
        if (action == OnlinePlayerAction::StopSpectating)
        {
            const int localPed = ResolveLocalTeleportEntity();
            const bool ok = NativeInvoker::InvokeVoid(
                NativeId::NetworkSetInSpectatorMode,
                std::int32_t{0},
                localPed);
            FinishAction(ok, ok ? "Spectating stopped" : "Could not stop spectating");
            return;
        }

        const int targetPed = ResolveTargetPed(playerId);
        if (targetPed == 0)
        {
            FinishAction(false, "Selected Online player is unavailable");
            return;
        }

        if (action == OnlinePlayerAction::Spectate)
        {
            const bool ok = NativeInvoker::InvokeVoid(
                NativeId::NetworkSetInSpectatorMode,
                std::int32_t{1},
                targetPed);
            FinishAction(ok, ok ? "Spectating selected player" : "Spectate request failed");
            return;
        }

        const auto coords = NativeInvoker::Invoke<Game::Native::NativeVector3>(
            NativeId::GetEntityCoords,
            targetPed,
            std::int32_t{0});
        if (!coords)
        {
            FinishAction(false, "Selected player position is unavailable");
            return;
        }

        if (action == OnlinePlayerAction::WaypointToPlayer)
        {
            const bool ok = NativeInvoker::InvokeVoid(
                NativeId::SetNewWaypoint,
                coords->x,
                coords->y);
            FinishAction(ok, ok ? "Waypoint set to selected player" : "Waypoint request failed");
            return;
        }

        if (action == OnlinePlayerAction::TeleportToPlayer)
        {
            int entity = ResolveLocalTeleportEntity();
            if (entity == 0)
            {
                FinishAction(false, "Local teleport entity is unavailable");
                return;
            }

            const bool ok = NativeInvoker::InvokeVoid(
                NativeId::SetEntityCoordsNoOffset,
                entity,
                coords->x,
                coords->y - 3.0f,
                coords->z + 0.6f,
                std::int32_t{1},
                std::int32_t{1},
                std::int32_t{1});
            FinishAction(ok, ok ? "Teleported to selected player" : "Teleport request failed");
            return;
        }

        FinishAction(false, "Unsupported player action");
    }

    void OnlinePlayerService::FinishAction(bool success, std::string message) noexcept
    {
        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot.message = std::move(message);
        }
        m_ActionPending.store(false, std::memory_order_release);
    }
}
