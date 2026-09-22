#include "FeatureRestore.hpp"

#include "SettingsService.hpp"
#include "../core/Logger.hpp"
#include "../features/player/PlayerService.hpp"
#include "../features/player/SelfOnlineService.hpp"
#include "../features/utility/UtilityService.hpp"
#include "../features/vehicle/VehicleService.hpp"
#include "../features/weapon/WeaponService.hpp"
#include "../features/world/TeleportService.hpp"
#include "../features/world/WorldService.hpp"

#include <algorithm>

namespace TutonesV2::Config
{
    void RestoreSavedFeatureState() noexcept
    {
        const auto settings = SettingsService::Get().Snapshot();

        auto& player = Features::Player::PlayerService::Get();
        static_cast<void>(player.SetGodMode(settings.selfGodMode));
        static_cast<void>(player.SetBulletproof(settings.selfBulletproof));
        static_cast<void>(player.SetInvisible(settings.selfInvisible));
        static_cast<void>(player.SetDisableCriticalHits(settings.selfDisableCriticalHits));
        static_cast<void>(player.SetKeepPlayerClean(settings.selfKeepClean));
        static_cast<void>(player.SetNoRagdoll(settings.selfNoRagdoll));
        static_cast<void>(player.SetNeverWanted(settings.selfNeverWanted));
        static_cast<void>(player.SetPoliceIgnore(settings.selfPoliceIgnore));
        static_cast<void>(player.SetEveryoneIgnore(settings.selfEveryoneIgnore));
        static_cast<void>(player.SetSuperJump(settings.selfSuperJump));
        static_cast<void>(player.SetInfiniteStamina(settings.selfInfiniteStamina));
        static_cast<void>(player.SetStandOnVehicles(settings.selfStandOnVehicles));
        static_cast<void>(player.SetDisableActionMode(settings.selfDisableActionMode));
        static_cast<void>(player.SetAquaLungs(settings.selfAquaLungs));
        static_cast<void>(player.SetInfiniteOxygen(settings.selfInfiniteOxygen));
        static_cast<void>(player.SetInfiniteParachutes(settings.selfInfiniteParachutes));
        static_cast<void>(player.SetMobileRadio(settings.selfMobileRadio));
        static_cast<void>(player.SetRunMultiplier(settings.selfRunMultiplier));
        static_cast<void>(player.SetSwimMultiplier(settings.selfSwimMultiplier));

        auto& selfOnline = Features::Player::SelfOnlineService::Get();
        static_cast<void>(selfOnline.SetMode(
            static_cast<Features::Player::RadarMode>(
                std::clamp(settings.selfRadarMode, 0, 2))));

        auto& weapons = Features::Weapon::WeaponService::Get();
        static_cast<void>(weapons.SetInfiniteAmmo(settings.weaponInfiniteAmmo));
        static_cast<void>(weapons.SetInfiniteClip(settings.weaponInfiniteClip));
        static_cast<void>(weapons.SetExplosiveAmmo(settings.weaponExplosiveAmmo));
        static_cast<void>(weapons.SetAimForHead(settings.weaponAimForHead));
        static_cast<void>(weapons.SetTargetDrivers(settings.weaponTargetDrivers));
        static_cast<void>(weapons.SetAimbot(settings.weaponAimbot));
        static_cast<void>(weapons.SetLaserSight(settings.weaponLaserSight));

        auto& vehicle = Features::Vehicle::VehicleService::Get();
        static_cast<void>(vehicle.SetVehicleGodMode(settings.vehicleGodMode));
        static_cast<void>(vehicle.SetKeepVehicleClean(settings.vehicleKeepClean));
        static_cast<void>(vehicle.SetHornBoost(settings.vehicleHornBoost));

        Features::World::TeleportService::Get().SetAutoWaypoint(
            settings.teleportAutoWaypoint);

        auto& world = Features::World::WorldService::Get();
        world.RestoreSavedState(
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

        auto& utilities = Features::Utility::UtilityService::Get();
        utilities.SetShowCoordinates(settings.miscShowCoordinates);
        utilities.SetShowHeading(settings.miscShowHeading);
        utilities.SetShowFps(settings.miscShowFps);
        utilities.SetShowSessionInfo(settings.miscShowSessionInfo);
        utilities.SetDisableCameraShake(settings.miscDisableCameraShake);
        utilities.Maintain();

        Core::Logger::Get().Info("settings", "Saved feature checkbox state restored after native runtime became Ready");
    }
}
