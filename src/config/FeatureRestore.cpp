#include "FeatureRestore.hpp"

#include "SettingsService.hpp"
#include "../core/Logger.hpp"
#include "../features/player/PlayerService.hpp"
#include "../features/player/SelfOnlineService.hpp"
#include "../features/vehicle/VehicleService.hpp"
#include "../features/weapon/WeaponService.hpp"
#include "../features/world/TeleportService.hpp"
#include "../features/world/WorldService.hpp"
#include "../game/GameRuntime.hpp"
#include "../game/native/NativeInvoker.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace TutonesV2::Config
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;

        struct RestoreState final
        {
            MenuSettings settings{};
            int warmupTicks{};
            int stage{};
        };

        std::atomic_bool g_RestoreScheduled{};

        bool PlayerReady() noexcept
        {
            const auto ped = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
            if (!ped || *ped == 0)
                return false;

            const auto exists = NativeInvoker::Invoke<std::int32_t>(
                NativeId::DoesEntityExist,
                *ped);
            return exists && *exists != 0;
        }

        bool HasWorldOverrides(const MenuSettings& settings) noexcept
        {
            return settings.worldFreezeClock
                || settings.worldBlackout
                || settings.worldWeatherOverride
                || std::fabs(settings.worldPedDensity - 1.0f) > 0.001f
                || std::fabs(settings.worldScenarioPedDensity - 1.0f) > 0.001f
                || std::fabs(settings.worldVehicleDensity - 1.0f) > 0.001f
                || std::fabs(settings.worldRandomVehicleDensity - 1.0f) > 0.001f
                || std::fabs(settings.worldParkedVehicleDensity - 1.0f) > 0.001f;
        }

        void FinishRestore() noexcept
        {
            g_RestoreScheduled.store(false, std::memory_order_release);
            Core::Logger::Get().Info(
                "settings",
                "Saved gameplay feature restore completed");
        }
    }

    void ScheduleSavedFeatureRestore() noexcept
    {
        bool expected = false;
        if (!g_RestoreScheduled.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel))
        {
            return;
        }

        auto state = std::make_shared<RestoreState>();
        state->settings = SettingsService::Get().Snapshot();

        auto tick = std::make_shared<std::function<void()>>();
        *tick = [state, tick]() {
            auto& runtime = Game::GameRuntime::Get();
            if (!runtime.NativeReady())
            {
                g_RestoreScheduled.store(false, std::memory_order_release);
                return;
            }

            // Do not restore gameplay state on the landing/loading screen.
            // Require a real player ped and then give GTA a short scheduler warmup.
            if (!PlayerReady() || state->warmupTicks < 120)
            {
                ++state->warmupTicks;
                if (state->warmupTicks == 1)
                    Core::Logger::Get().Info(
                        "settings",
                        "Saved feature restore waiting for a stable player ped");

                if (state->warmupTicks > 900)
                {
                    Core::Logger::Get().Warn(
                        "settings",
                        "Saved feature restore timed out waiting for player readiness");
                    g_RestoreScheduled.store(false, std::memory_order_release);
                    return;
                }

                if (!runtime.Enqueue(*tick))
                    g_RestoreScheduled.store(false, std::memory_order_release);
                return;
            }

            const auto& settings = state->settings;

            switch (state->stage++)
            {
            case 0:
            {
                Core::Logger::Get().Info("settings.restore", "Stage 1: Self protections");
                auto& player = Features::Player::PlayerService::Get();
                if (settings.selfGodMode) static_cast<void>(player.SetGodMode(true));
                if (settings.selfBulletproof) static_cast<void>(player.SetBulletproof(true));
                if (settings.selfInvisible) static_cast<void>(player.SetInvisible(true));
                if (settings.selfDisableCriticalHits) static_cast<void>(player.SetDisableCriticalHits(true));
                if (settings.selfKeepClean) static_cast<void>(player.SetKeepPlayerClean(true));
                if (settings.selfNoRagdoll) static_cast<void>(player.SetNoRagdoll(true));
                break;
            }

            case 1:
            {
                Core::Logger::Get().Info("settings.restore", "Stage 2: Self movement and utilities");
                auto& player = Features::Player::PlayerService::Get();
                if (settings.selfNeverWanted) static_cast<void>(player.SetNeverWanted(true));
                if (settings.selfPoliceIgnore) static_cast<void>(player.SetPoliceIgnore(true));
                if (settings.selfEveryoneIgnore) static_cast<void>(player.SetEveryoneIgnore(true));
                if (settings.selfSuperJump) static_cast<void>(player.SetSuperJump(true));
                if (settings.selfInfiniteStamina) static_cast<void>(player.SetInfiniteStamina(true));
                if (settings.selfStandOnVehicles) static_cast<void>(player.SetStandOnVehicles(true));
                if (settings.selfDisableActionMode) static_cast<void>(player.SetDisableActionMode(true));
                if (settings.selfAquaLungs) static_cast<void>(player.SetAquaLungs(true));
                if (settings.selfInfiniteOxygen) static_cast<void>(player.SetInfiniteOxygen(true));
                if (settings.selfInfiniteParachutes) static_cast<void>(player.SetInfiniteParachutes(true));
                if (settings.selfMobileRadio) static_cast<void>(player.SetMobileRadio(true));
                if (settings.selfRunMultiplier > 1.001f)
                    static_cast<void>(player.SetRunMultiplier(settings.selfRunMultiplier));
                if (settings.selfSwimMultiplier > 1.001f)
                    static_cast<void>(player.SetSwimMultiplier(settings.selfSwimMultiplier));
                break;
            }

            case 2:
            {
                Core::Logger::Get().Info("settings.restore", "Stage 3: Weapon ammo state");
                auto& weapons = Features::Weapon::WeaponService::Get();
                if (settings.weaponInfiniteAmmo) static_cast<void>(weapons.SetInfiniteAmmo(true));
                if (settings.weaponInfiniteClip) static_cast<void>(weapons.SetInfiniteClip(true));
                if (settings.weaponExplosiveAmmo) static_cast<void>(weapons.SetExplosiveAmmo(true));
                break;
            }

            case 3:
            {
                Core::Logger::Get().Info(
                    "settings.restore",
                    "Stage 4: Risky weapon state held for manual test");

                if (settings.weaponAimbot)
                    Core::Logger::Get().Warn(
                        "settings.restore",
                        "Saved Aimbot remains disabled on injection; enable it manually after GTA is fully loaded");
                if (settings.weaponLaserSight)
                    Core::Logger::Get().Warn(
                        "settings.restore",
                        "Saved Laser Sight remains disabled on injection; enable it manually after GTA is fully loaded");
                break;
            }

            case 4:
            {
                Core::Logger::Get().Info("settings.restore", "Stage 5: Vehicle and teleport state");
                auto& vehicle = Features::Vehicle::VehicleService::Get();
                if (settings.vehicleGodMode) static_cast<void>(vehicle.SetVehicleGodMode(true));
                if (settings.vehicleKeepClean) static_cast<void>(vehicle.SetKeepVehicleClean(true));
                if (settings.vehicleHornBoost) static_cast<void>(vehicle.SetHornBoost(true));

                if (settings.teleportAutoWaypoint)
                    Features::World::TeleportService::Get().SetAutoWaypoint(true);
                break;
            }

            case 5:
            {
                Core::Logger::Get().Info("settings.restore", "Stage 6: World state");
                if (HasWorldOverrides(settings))
                {
                    Features::World::WorldService::Get().RestoreSavedState(
                        settings.worldFreezeClock,
                        settings.worldBlackout,
                        settings.worldWeatherOverride,
                        settings.worldWeatherIndex,
                        settings.worldPedDensity,
                        settings.worldScenarioPedDensity,
                        settings.worldVehicleDensity,
                        settings.worldRandomVehicleDensity,
                        settings.worldParkedVehicleDensity,
                        settings.worldHour,
                        settings.worldMinute);
                }
                break;
            }

            case 6:
            {
                Core::Logger::Get().Info("settings.restore", "Stage 7: Online Self state");
                if (settings.selfRadarMode != 0)
                {
                    static_cast<void>(
                        Features::Player::SelfOnlineService::Get().SetMode(
                            static_cast<Features::Player::RadarMode>(
                                std::clamp(settings.selfRadarMode, 0, 2))));
                }
                break;
            }

            default:
                FinishRestore();
                return;
            }

            if (!runtime.Enqueue(*tick))
                g_RestoreScheduled.store(false, std::memory_order_release);
        };

        if (!Game::GameRuntime::Get().Enqueue(*tick))
        {
            g_RestoreScheduled.store(false, std::memory_order_release);
            Core::Logger::Get().Warn(
                "settings",
                "Could not schedule saved gameplay feature restore");
        }
        else
        {
            Core::Logger::Get().Info(
                "settings",
                "Saved gameplay feature restore scheduled safely");
        }
    }
}
