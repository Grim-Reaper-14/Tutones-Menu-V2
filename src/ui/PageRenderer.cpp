#include "PageRenderer.hpp"

#include "MenuTheme.hpp"
#include "../backend/BackendHub.hpp"
#include "../config/SettingsService.hpp"
#include "../features/player/PlayerService.hpp"
#include "../features/player/PlayerStatsService.hpp"
#include "../features/player/SelfOnlineService.hpp"
#include "../features/online/OnlineStatusService.hpp"
#include "../features/utility/UtilityService.hpp"
#include "../features/vehicle/VehicleService.hpp"
#include "../features/weapon/WeaponService.hpp"
#include "../features/world/TeleportService.hpp"
#include "../features/world/WorldService.hpp"
#include "../game/GameRuntime.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace TutonesV2::UI
{
    namespace
    {
        template<std::size_t Size>
        void CopySettingText(char (&destination)[Size], const std::string& source) noexcept
        {
            static_assert(Size > 0);
            std::memset(destination, 0, Size);
            const auto count = std::min(source.size(), Size - 1);
            std::memcpy(destination, source.data(), count);
        }

        void PlannedSection(const char* title, const char* description, const char* backend) noexcept
        {
            ImGui::SeparatorText(title);
            ImGui::TextWrapped("%s", description);
            ImGui::TextDisabled("Backend slot: %s", backend);
            ImGui::Spacing();
        }

        void RenderSelf() noexcept
        {
            using Features::Player::PlayerService;
            using Features::Player::PlayerStatsService;
            using Features::Player::RadarMode;
            using Features::Player::SelfOnlineService;

            auto& player = PlayerService::Get();
            auto& online = SelfOnlineService::Get();
            auto& stats = PlayerStatsService::Get();
            auto& runtime = Game::GameRuntime::Get();

            const bool nativeReady = runtime.NativeReady();
            const auto onlineState = online.Snapshot();
            const auto statsState = stats.Snapshot();

            static std::string status{"Ready"};
            static bool settingsLoaded{};
            static int health{200};
            static int armor{100};
            static int wantedLevel{};
            static char modelName[64]{"mp_m_freemode_01"};
            static int component{};
            static int drawable{};
            static int texture{};
            static int palette{};

            if (!settingsLoaded)
            {
                const auto saved = Config::SettingsService::Get().Snapshot();
                health = saved.selfHealth;
                armor = saved.selfArmor;
                wantedLevel = saved.selfWantedLevel;
                CopySettingText(modelName, saved.selfPedModel);
                component = saved.selfComponent;
                drawable = saved.selfDrawable;
                texture = saved.selfTexture;
                palette = saved.selfPalette;
                settingsLoaded = true;
            }

            static std::uint64_t lastStatsRevision{};
            static int rank{1};
            static int rp{};
            static int kills{};
            static int deaths{};
            static float kdRatio{};

            if (statsState.revision != lastStatsRevision)
            {
                lastStatsRevision = statsState.revision;
                if (statsState.readable)
                {
                    rank = statsState.rank;
                    rp = statsState.rp;
                    kills = statsState.kills;
                    deaths = statsState.deaths;
                    kdRatio = statsState.kdRatio;
                }
            }

            ImGui::SeparatorText("Runtime");
            ImGui::BulletText("Native runtime: %s", Game::GameRuntime::NativeStateName(runtime.NativeState()));
            ImGui::BulletText("Self service: %s", player.IsReady() ? "ready" : "offline");
            if (!nativeReady)
                ImGui::TextDisabled("Self controls stay locked until the native canary reaches Ready.");

            ImGui::BeginDisabled(!player.IsReady() || !nativeReady);

            if (ImGui::CollapsingHeader("Protection & Player State", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool godMode = player.GodMode();
                bool bulletproof = player.Bulletproof();
                bool invisible = player.Invisible();
                bool disableCriticalHits = player.DisableCriticalHits();
                bool keepClean = player.KeepPlayerClean();
                bool noRagdoll = player.NoRagdoll();

                if (ImGui::Checkbox("God Mode", &godMode))
                    status = player.SetGodMode(godMode) ? "God Mode updated" : "God Mode request rejected";
                if (ImGui::Checkbox("Bulletproof / Semi-God", &bulletproof))
                    status = player.SetBulletproof(bulletproof) ? "Bulletproof updated" : "Bulletproof request rejected";
                if (ImGui::Checkbox("Invisible", &invisible))
                    status = player.SetInvisible(invisible) ? "Invisible updated" : "Invisible request rejected";
                if (ImGui::Checkbox("Disable Critical Hits", &disableCriticalHits))
                    status = player.SetDisableCriticalHits(disableCriticalHits) ? "Critical hits updated" : "Critical-hit request rejected";
                if (ImGui::Checkbox("Keep Player Clean", &keepClean))
                    status = player.SetKeepPlayerClean(keepClean) ? "Keep Player Clean updated" : "Keep Player Clean request rejected";
                if (ImGui::Checkbox("No Ragdoll", &noRagdoll))
                    status = player.SetNoRagdoll(noRagdoll) ? "No Ragdoll updated" : "No Ragdoll request rejected";
            }

            if (ImGui::CollapsingHeader("Health, Armor & Wanted"))
            {
                health = std::max(0, health);
                armor = std::clamp(armor, 0, 100);
                wantedLevel = std::clamp(wantedLevel, 0, 5);

                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputInt("Health", &health, 10, 50))
                {
                    health = std::max(0, health);
                    const int value = health;
                    Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                        settings.selfHealth = value;
                    });
                }
                ImGui::SameLine();
                if (ImGui::Button("Set Health"))
                    status = player.QueueSetHealth(health) ? "Set Health queued" : "Set Health rejected";
                ImGui::SameLine();
                if (ImGui::Button("Full Heal"))
                    status = player.QueueHeal() ? "Full Heal queued" : "Full Heal rejected";

                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputInt("Armor", &armor, 10, 25))
                {
                    armor = std::clamp(armor, 0, 100);
                    const int value = armor;
                    Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                        settings.selfArmor = value;
                    });
                }
                ImGui::SameLine();
                if (ImGui::Button("Set Armor"))
                    status = player.QueueSetArmor(armor) ? "Set Armor queued" : "Set Armor rejected";

                bool neverWanted = player.NeverWanted();
                bool policeIgnore = player.PoliceIgnore();
                bool everyoneIgnore = player.EveryoneIgnore();

                if (ImGui::Checkbox("Never Wanted", &neverWanted))
                    status = player.SetNeverWanted(neverWanted) ? "Never Wanted updated" : "Never Wanted request rejected";
                if (ImGui::Checkbox("Police Ignore", &policeIgnore))
                    status = player.SetPoliceIgnore(policeIgnore) ? "Police Ignore updated" : "Police Ignore request rejected";
                if (ImGui::Checkbox("Everyone Ignore", &everyoneIgnore))
                    status = player.SetEveryoneIgnore(everyoneIgnore) ? "Everyone Ignore updated" : "Everyone Ignore request rejected";

                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputInt("Wanted Level", &wantedLevel, 1, 1))
                {
                    wantedLevel = std::clamp(wantedLevel, 0, 5);
                    const int value = wantedLevel;
                    Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                        settings.selfWantedLevel = value;
                    });
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply Wanted"))
                    status = player.QueueSetWantedLevel(wantedLevel) ? "Wanted level queued" : "Wanted-level request rejected";
                ImGui::SameLine();
                if (ImGui::Button("Clear Wanted"))
                    status = player.QueueClearWanted() ? "Clear Wanted queued" : "Clear Wanted rejected";

                if (ImGui::Button("Clean Player"))
                    status = player.QueueCleanPlayer() ? "Clean Player queued" : "Clean Player rejected";
                ImGui::SameLine();
                if (ImGui::Button("Suicide"))
                    status = player.QueueSuicide() ? "Suicide queued" : "Suicide rejected";
            }

            if (ImGui::CollapsingHeader("Movement & Oxygen"))
            {
                bool superJump = player.SuperJump();
                bool infiniteStamina = player.InfiniteStamina();
                bool standOnVehicles = player.StandOnVehicles();
                bool disableActionMode = player.DisableActionMode();
                bool aquaLungs = player.AquaLungs();
                bool infiniteOxygen = player.InfiniteOxygen();
                float runMultiplier = player.RunMultiplier();
                float swimMultiplier = player.SwimMultiplier();

                if (ImGui::Checkbox("Super Jump", &superJump))
                    status = player.SetSuperJump(superJump) ? "Super Jump updated" : "Super Jump request rejected";
                if (ImGui::Checkbox("Infinite Stamina", &infiniteStamina))
                    status = player.SetInfiniteStamina(infiniteStamina) ? "Infinite Stamina updated" : "Infinite Stamina request rejected";
                if (ImGui::Checkbox("Stand On Vehicles", &standOnVehicles))
                    status = player.SetStandOnVehicles(standOnVehicles) ? "Stand On Vehicles updated" : "Stand On Vehicles request rejected";
                if (ImGui::Checkbox("Disable Action Mode", &disableActionMode))
                    status = player.SetDisableActionMode(disableActionMode) ? "Disable Action Mode updated" : "Disable Action Mode rejected";
                if (ImGui::Checkbox("Aqua Lungs", &aquaLungs))
                    status = player.SetAquaLungs(aquaLungs) ? "Aqua Lungs updated" : "Aqua Lungs request rejected";
                if (ImGui::Checkbox("Infinite Oxygen", &infiniteOxygen))
                    status = player.SetInfiniteOxygen(infiniteOxygen) ? "Infinite Oxygen updated" : "Infinite Oxygen request rejected";

                if (ImGui::SliderFloat("Run / Sprint", &runMultiplier, 1.0f, 1.49f, "%.2fx"))
                    status = player.SetRunMultiplier(runMultiplier) ? "Run multiplier updated" : "Run multiplier rejected";
                if (ImGui::SliderFloat("Swim", &swimMultiplier, 1.0f, 1.49f, "%.2fx"))
                    status = player.SetSwimMultiplier(swimMultiplier) ? "Swim multiplier updated" : "Swim multiplier rejected";
            }

            if (ImGui::CollapsingHeader("Utilities"))
            {
                bool infiniteParachutes = player.InfiniteParachutes();
                bool mobileRadio = player.MobileRadio();

                if (ImGui::Checkbox("Infinite Parachutes", &infiniteParachutes))
                    status = player.SetInfiniteParachutes(infiniteParachutes) ? "Infinite Parachutes updated" : "Infinite Parachutes rejected";
                if (ImGui::Checkbox("Mobile Radio", &mobileRadio))
                    status = player.SetMobileRadio(mobileRadio) ? "Mobile Radio updated" : "Mobile Radio rejected";
            }

            if (ImGui::CollapsingHeader("Appearance / Model"))
            {
                ImGui::SetNextItemWidth(240.0f);
                if (ImGui::InputText("Ped Model", modelName, sizeof(modelName)))
                {
                    const std::string value(modelName);
                    Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                        settings.selfPedModel = value;
                    });
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(player.ModelLoadPending());
                if (ImGui::Button("Load Ped Model"))
                    status = player.QueueModelByName(modelName) ? "Ped model request queued" : "Ped model request rejected";
                ImGui::EndDisabled();

                component = std::clamp(component, 0, 11);
                drawable = std::max(0, drawable);
                texture = std::max(0, texture);
                palette = std::clamp(palette, 0, 3);

                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::InputInt("Component", &component, 1, 1))
                {
                    component = std::clamp(component, 0, 11);
                    const int value = component;
                    Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                        settings.selfComponent = value;
                    });
                }
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::InputInt("Drawable", &drawable, 1, 5))
                {
                    drawable = std::max(0, drawable);
                    const int value = drawable;
                    Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                        settings.selfDrawable = value;
                    });
                }
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::InputInt("Texture", &texture, 1, 5))
                {
                    texture = std::max(0, texture);
                    const int value = texture;
                    Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                        settings.selfTexture = value;
                    });
                }
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::InputInt("Palette", &palette, 1, 1))
                {
                    palette = std::clamp(palette, 0, 3);
                    const int value = palette;
                    Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                        settings.selfPalette = value;
                    });
                }

                if (ImGui::Button("Apply Component"))
                    status = player.QueueSetComponent(component, drawable, texture, palette)
                        ? "Component change queued" : "Component change rejected";
                ImGui::SameLine();
                if (ImGui::Button("Default Outfit"))
                    status = player.QueueDefaultComponents() ? "Default outfit queued" : "Default outfit rejected";
                ImGui::SameLine();
                if (ImGui::Button("Random Outfit"))
                    status = player.QueueRandomComponents() ? "Random outfit queued" : "Random outfit rejected";

                if (player.ModelLoadPending())
                    ImGui::TextDisabled("Waiting for ped model to stream...");
            }

            ImGui::EndDisabled();

            if (ImGui::CollapsingHeader("Off Radar / Ghost Organization"))
            {
                int radarMode = static_cast<int>(online.Mode());
                constexpr std::array<const char*, 3> modes{{"Off", "Off Radar", "Ghost Organization"}};

                ImGui::BeginDisabled(!nativeReady || !online.IsReady());
                ImGui::SetNextItemWidth(210.0f);
                if (ImGui::Combo("Radar Mode", &radarMode, modes.data(), static_cast<int>(modes.size())))
                {
                    const auto requested = static_cast<RadarMode>(std::clamp(radarMode, 0, 2));
                    status = online.SetMode(requested)
                        ? "Online radar mode updated"
                        : "Online radar mode rejected";
                }
                ImGui::EndDisabled();

                ImGui::Text("Off Radar: %s", onlineState.offRadarApplied ? "ACTIVE" : "OFF");
                ImGui::SameLine();
                ImGui::Text("Ghost Org: %s", onlineState.ghostOrganizationApplied ? "ACTIVE" : "OFF");
                ImGui::TextDisabled("%s", onlineState.message.c_str());
                if (!onlineState.safeToModify)
                    ImGui::TextDisabled("Requires a live GTA Online freemode session and verified Enhanced globals.");
            }

            if (ImGui::CollapsingHeader("Online Stats Editor"))
            {
                ImGui::TextDisabled("Cash and Bank are intentionally not writable here.");

                if (statsState.readable)
                    ImGui::Text("Active character: MP%d", statsState.characterIndex);

                const int previousRank = rank;
                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputInt("Rank", &rank, 1, 10);
                rank = std::clamp(rank, 1, 8000);
                if (rank != previousRank)
                {
                    if (const auto requiredRp = PlayerStatsService::RpForRank(rank))
                        rp = *requiredRp;
                }

                rp = std::max(0, rp);
                kills = std::max(0, kills);
                deaths = std::max(0, deaths);

                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputInt("RP", &rp, 100, 1000);

                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::InputFloat("K/D Ratio", &kdRatio, 0.0f, 0.0f, "%.2f"))
                {
                    kdRatio = std::clamp(kdRatio, 0.0f, 10000.0f);
                    const int denominator = std::max(1, deaths);
                    const double requestedKills =
                        static_cast<double>(kdRatio) * static_cast<double>(denominator);
                    kills = requestedKills >= static_cast<double>(std::numeric_limits<int>::max())
                        ? std::numeric_limits<int>::max()
                        : static_cast<int>(std::llround(requestedKills));
                }

                const int previousKills = kills;
                const int previousDeaths = deaths;
                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputInt("Kills", &kills, 1, 10);
                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputInt("Deaths", &deaths, 1, 10);
                kills = std::max(0, kills);
                deaths = std::max(0, deaths);

                if (kills != previousKills || deaths != previousDeaths)
                {
                    kdRatio = deaths > 0
                        ? static_cast<float>(kills) / static_cast<float>(deaths)
                        : static_cast<float>(kills);
                }

                ImGui::BeginDisabled(!stats.IsReady() || !nativeReady || statsState.pending);
                if (ImGui::Button("Apply Stats"))
                    status = stats.QueueApply(rank, rp, kills, deaths)
                        ? "Stat writes queued" : "Stat writes rejected";
                ImGui::SameLine();
                if (ImGui::Button("Refresh Current Stats"))
                    status = stats.QueueRefresh()
                        ? "Stat refresh queued" : "Stat refresh rejected";
                ImGui::EndDisabled();

                ImGui::TextDisabled("%s", statsState.message.c_str());
            }

            ImGui::SeparatorText("Quick Test");
            ImGui::BeginDisabled(!player.IsReady() || !nativeReady);
            if (ImGui::Button("Heal"))
                status = player.QueueHeal() ? "Heal queued" : "Heal rejected";
            ImGui::SameLine();
            if (ImGui::Button("Armor 100"))
                status = player.QueueSetArmor(100) ? "Armor queued" : "Armor rejected";
            ImGui::SameLine();
            if (ImGui::Button("Clear Wanted##self_quick"))
                status = player.QueueClearWanted() ? "Clear Wanted queued" : "Clear Wanted rejected";
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled();
            ImGui::Button("Request Job");
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextWrapped("Last Self action: %s", status.c_str());
            ImGui::TextDisabled("Request Job remains disabled until a current Enhanced job-request path is verified.");
        }

        void RenderWeapons() noexcept
        {
            auto& service = Features::Weapon::WeaponService::Get();
            const bool nativeReady = Game::GameRuntime::Get().NativeReady();
            static std::string status{"Ready"};
            static bool settingsLoaded{};
            static char weaponName[64]{"WEAPON_CARBINERIFLE"};

            if (!settingsLoaded)
            {
                CopySettingText(
                    weaponName,
                    Config::SettingsService::Get().Snapshot().weaponName);
                settingsLoaded = true;
            }

            ImGui::SeparatorText("Weapon Runtime");
            ImGui::BulletText("Native runtime: %s", nativeReady ? "READY" : "WAITING");
            ImGui::BeginDisabled(!service.IsReady() || !nativeReady);

            bool infiniteAmmo = service.InfiniteAmmo();
            bool infiniteClip = service.InfiniteClip();
            bool explosiveAmmo = service.ExplosiveAmmo();
            bool aimbot = service.Aimbot();
            bool aimForHead = service.AimForHead();
            bool targetDrivers = service.TargetDrivers();
            bool laserSight = service.LaserSight();

            if (ImGui::Checkbox("Infinite Ammo", &infiniteAmmo))
                status = service.SetInfiniteAmmo(infiniteAmmo) ? "Infinite Ammo updated" : "Infinite Ammo rejected";
            if (ImGui::Checkbox("Infinite Clip", &infiniteClip))
                status = service.SetInfiniteClip(infiniteClip) ? "Infinite Clip updated" : "Infinite Clip rejected";
            if (ImGui::Checkbox("Explosive Ammo", &explosiveAmmo))
                status = service.SetExplosiveAmmo(explosiveAmmo) ? "Explosive Ammo updated" : "Explosive Ammo rejected";

            ImGui::SeparatorText("Aim Assist");
            ImGui::TextDisabled(
                "Aimbot: %s | Head: %s | Drivers: %s",
                service.AimbotSupported() ? "READY" : "UNAVAILABLE",
                service.AimForHeadSupported() ? "READY" : "UNAVAILABLE",
                service.TargetDriversSupported() ? "READY" : "UNAVAILABLE");

            ImGui::BeginDisabled(!service.AimbotSupported());
            if (ImGui::Checkbox("Aimbot", &aimbot))
                status = service.SetAimbot(aimbot) ? "Aimbot updated" : "Aimbot rejected";
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!aimbot || !service.AimForHeadSupported());
            if (ImGui::Checkbox("Aim For Head", &aimForHead))
                status = service.SetAimForHead(aimForHead) ? "Aim For Head updated" : "Aim For Head rejected";
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!aimbot || !service.TargetDriversSupported());
            if (ImGui::Checkbox("Target Drivers", &targetDrivers))
                status = service.SetTargetDrivers(targetDrivers) ? "Target Drivers updated" : "Target Drivers rejected";
            ImGui::EndDisabled();

            if (ImGui::Checkbox("Laser Sight (Native)", &laserSight))
                status = service.SetLaserSight(laserSight) ? "Native Laser Sight updated" : "Native Laser Sight rejected";
            ImGui::TextDisabled("Uses ENABLE_LASER_SIGHT_RENDERING");

            ImGui::SeparatorText("Weapon Utilities");
            if (ImGui::Button("Give All Weapons"))
                status = service.QueueGiveAllWeapons() ? "Give All Weapons queued" : "Give All Weapons rejected";
            ImGui::SameLine();
            if (ImGui::Button("Max Ammo"))
                status = service.QueueMaxAmmo() ? "Max Ammo queued" : "Max Ammo rejected";

            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::InputText("Weapon Name", weaponName, sizeof(weaponName)))
            {
                const std::string value(weaponName);
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.weaponName = value;
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Give Weapon"))
                status = service.QueueGiveWeapon(weaponName) ? "Give Weapon queued" : "Give Weapon rejected";

            ImGui::EndDisabled();
            ImGui::TextWrapped("Last weapon action: %s", status.c_str());
        }

        void RenderVehicle() noexcept
        {
            auto& service = Features::Vehicle::VehicleService::Get();
            const auto snapshot = service.Snapshot();
            const bool nativeReady = Game::GameRuntime::Get().NativeReady();
            static bool settingsLoaded{};
            static char modelName[64]{"adder"};
            static bool enterVehicle = true;
            static bool networked = true;

            if (!settingsLoaded)
            {
                const auto saved = Config::SettingsService::Get().Snapshot();
                CopySettingText(modelName, saved.vehicleModel);
                enterVehicle = saved.vehicleEnterAfterSpawn;
                networked = saved.vehicleNetworked;
                settingsLoaded = true;
            }

            ImGui::SeparatorText("Vehicle Spawner");
            ImGui::BeginDisabled(!service.IsReady() || !nativeReady || snapshot.busy);
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::InputText("Vehicle Model", modelName, sizeof(modelName)))
            {
                const std::string value(modelName);
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.vehicleModel = value;
                });
            }
            if (ImGui::Checkbox("Enter spawned vehicle", &enterVehicle))
            {
                const bool value = enterVehicle;
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.vehicleEnterAfterSpawn = value;
                });
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Networked / persistent", &networked))
            {
                const bool value = networked;
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.vehicleNetworked = value;
                });
            }
            if (ImGui::Button("Spawn Vehicle"))
                static_cast<void>(service.QueueSpawn(modelName, enterVehicle, networked));
            ImGui::EndDisabled();

            ImGui::TextDisabled("%s", snapshot.message.c_str());
            if (snapshot.lastSpawnedVehicle)
                ImGui::Text("Last spawned handle: %d", snapshot.lastSpawnedVehicle);

            ImGui::SeparatorText("Current Vehicle");
            ImGui::BeginDisabled(!service.IsReady() || !nativeReady);

            bool vehicleGodMode = service.VehicleGodMode();
            bool keepVehicleClean = service.KeepVehicleClean();
            bool hornBoost = service.HornBoost();

            if (ImGui::Checkbox("Vehicle God Mode", &vehicleGodMode))
                static_cast<void>(service.SetVehicleGodMode(vehicleGodMode));
            if (ImGui::Checkbox("Keep Vehicle Clean", &keepVehicleClean))
                static_cast<void>(service.SetKeepVehicleClean(keepVehicleClean));
            if (ImGui::Checkbox("Horn Boost", &hornBoost))
                static_cast<void>(service.SetHornBoost(hornBoost));

            if (ImGui::Button("Repair Current"))
                static_cast<void>(service.QueueRepairCurrent());
            ImGui::SameLine();
            if (ImGui::Button("Clean Current"))
                static_cast<void>(service.QueueCleanCurrent());
            ImGui::SameLine();
            if (ImGui::Button("Set Upright"))
                static_cast<void>(service.QueueSetUpright());

            ImGui::EndDisabled();
            ImGui::TextDisabled("Horn Boost uses the normal horn control while driving.");
        }

        void RenderTeleport() noexcept
        {
            auto& service = Features::World::TeleportService::Get();
            const auto snapshot = service.Snapshot();
            const bool nativeReady = Game::GameRuntime::Get().NativeReady();

            static bool settingsLoaded{};
            static float coords[3]{0.0f, 0.0f, 0.0f};
            static bool resolveGround = true;
            static float directionalDistance = 5.0f;

            if (!settingsLoaded)
            {
                const auto saved = Config::SettingsService::Get().Snapshot();
                coords[0] = saved.teleportX;
                coords[1] = saved.teleportY;
                coords[2] = saved.teleportZ;
                resolveGround = saved.teleportResolveGround;
                directionalDistance = saved.teleportDirectionalDistance;
                settingsLoaded = true;
            }

            ImGui::SeparatorText("YimMenuV2 Teleport");
            ImGui::TextDisabled("Waypoint Z-resolution mirrors YimMenuV2: collision request, 20 yielded ground attempts, water check, then approximate-height fallback.");

            ImGui::BeginDisabled(!service.IsReady() || !nativeReady || snapshot.pending);

            if (ImGui::Button("Teleport to Waypoint"))
                static_cast<void>(service.QueueWaypoint());
            ImGui::SameLine();
            if (ImGui::Button("Teleport to Objective"))
                static_cast<void>(service.QueueObjective());

            ImGui::EndDisabled();

            bool autoWaypoint = snapshot.autoWaypointEnabled;
            ImGui::BeginDisabled(!service.IsReady() || !nativeReady);
            if (ImGui::Checkbox("Auto Teleport to Waypoint", &autoWaypoint))
                service.SetAutoWaypoint(autoWaypoint);
            ImGui::EndDisabled();

            ImGui::SeparatorText("Directional Teleport");
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderFloat("Distance", &directionalDistance, 1.0f, 100.0f, "%.1f"))
            {
                const float value = directionalDistance;
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.teleportDirectionalDistance = value;
                });
            }

            ImGui::BeginDisabled(!service.IsReady() || !nativeReady || snapshot.pending);
            if (ImGui::Button("Forward"))
                static_cast<void>(service.QueueDirectional(0.0f, directionalDistance, 0.0f));
            ImGui::SameLine();
            if (ImGui::Button("Backward"))
                static_cast<void>(service.QueueDirectional(0.0f, -directionalDistance, 0.0f));
            ImGui::SameLine();
            if (ImGui::Button("Left"))
                static_cast<void>(service.QueueDirectional(-directionalDistance, 0.0f, 0.0f));
            ImGui::SameLine();
            if (ImGui::Button("Right"))
                static_cast<void>(service.QueueDirectional(directionalDistance, 0.0f, 0.0f));

            if (ImGui::Button("Up"))
                static_cast<void>(service.QueueDirectional(0.0f, 0.0f, directionalDistance));
            ImGui::SameLine();
            if (ImGui::Button("Down"))
                static_cast<void>(service.QueueDirectional(0.0f, 0.0f, -directionalDistance));

            ImGui::SeparatorText("Coordinates");
            if (ImGui::InputFloat3("XYZ", coords))
            {
                const float x = coords[0];
                const float y = coords[1];
                const float z = coords[2];
                Config::SettingsService::Get().Update([x, y, z](Config::MenuSettings& settings) {
                    settings.teleportX = x;
                    settings.teleportY = y;
                    settings.teleportZ = z;
                });
            }
            if (ImGui::Checkbox("Resolve ground / water like YimMenuV2", &resolveGround))
            {
                const bool value = resolveGround;
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.teleportResolveGround = value;
                });
            }
            if (ImGui::Button("Teleport to Coordinates"))
                static_cast<void>(service.QueueCoordinates(coords[0], coords[1], coords[2], resolveGround));
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextWrapped("Teleport status: %s", snapshot.message.c_str());
            if (snapshot.pending)
                ImGui::TextDisabled("Resolving destination on the GTA scheduler...");
        }

        void RenderOnline() noexcept
        {
            auto& statusService = Features::Online::OnlineStatusService::Get();
            auto& selfOnline = Features::Player::SelfOnlineService::Get();

            statusService.RequestRefresh();
            const auto status = statusService.Snapshot();
            const auto radar = selfOnline.Snapshot();

            ImGui::SeparatorText("Session Status");
            ImGui::BulletText("Session: %s", status.sessionStarted ? "ONLINE" : "OFFLINE");
            ImGui::BulletText("Script globals: %s", status.globalsReady ? "READY" : "WAITING");
            ImGui::BulletText("Freemode thread: %s", status.freemodeReady ? "READY" : "WAITING");
            ImGui::BulletText("Network time: %s", status.networkTimeReady ? "READY" : "WAITING");
            ImGui::BulletText("Local player ID: %d", status.localPlayer);
            ImGui::BulletText(
                "Script threads: %d active / %d total",
                status.activeScriptThreads,
                status.scriptThreadCount);
            if (status.networkTimeReady)
                ImGui::BulletText("Network time value: %u", status.networkTime);
            ImGui::TextDisabled("%s", status.message.c_str());

            ImGui::SeparatorText("Player State");
            int radarMode = static_cast<int>(selfOnline.Mode());
            constexpr std::array<const char*, 3> radarModes{{"Off", "Off Radar", "Ghost Organization"}};

            ImGui::BeginDisabled(!Game::GameRuntime::Get().NativeReady() || !selfOnline.IsReady());
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::Combo(
                    "Radar / Organization",
                    &radarMode,
                    radarModes.data(),
                    static_cast<int>(radarModes.size())))
            {
                static_cast<void>(selfOnline.SetMode(
                    static_cast<Features::Player::RadarMode>(std::clamp(radarMode, 0, 2))));
            }
            ImGui::EndDisabled();

            ImGui::Text("Off Radar: %s", radar.offRadarApplied ? "ACTIVE" : "OFF");
            ImGui::SameLine();
            ImGui::Text("Ghost Org: %s", radar.ghostOrganizationApplied ? "ACTIVE" : "OFF");
            ImGui::TextDisabled("%s", radar.message.c_str());

            ImGui::SeparatorText("V1 Expansion");
            ImGui::TextWrapped(
                "Player roster, services, session switching and protection controls are the next Online backend pass. "
                "This page is already using the same session/globals/freemode readiness gates as the current V2 runtime.");
        }

        void RenderWorld() noexcept
        {
            auto& service = Features::World::WorldService::Get();
            const auto state = service.Snapshot();

            static int hour = 12;
            static int minute = 0;
            static int weatherIndex = 1;
            static float clearRadius = 75.0f;

            if (state.selectedHour >= 0 && state.selectedHour <= 23)
                hour = state.selectedHour;
            if (state.selectedMinute >= 0 && state.selectedMinute <= 59)
                minute = state.selectedMinute;
            weatherIndex = std::clamp(state.weatherIndex, 0, static_cast<int>(Features::World::WeatherCodes.size()) - 1);

            ImGui::BeginDisabled(!state.ready || state.actionPending);

            ImGui::SeparatorText("Time");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("Hour", &hour, 1, 1);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("Minute", &minute, 1, 5);
            hour = std::clamp(hour, 0, 23);
            minute = std::clamp(minute, 0, 59);

            if (ImGui::Button("Apply Time"))
                static_cast<void>(service.QueueSetTime(hour, minute));
            ImGui::SameLine();
            if (ImGui::Button("Refresh Clock"))
                service.RequestClockSample();

            bool freezeClock = state.freezeClock;
            if (ImGui::Checkbox("Freeze Time", &freezeClock))
                static_cast<void>(service.SetFreezeClock(freezeClock));

            if (state.clockHour >= 0 && state.clockMinute >= 0)
                ImGui::TextDisabled("Observed GTA clock: %02d:%02d", state.clockHour, state.clockMinute);

            ImGui::SeparatorText("Weather");
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::Combo(
                    "Weather",
                    &weatherIndex,
                    Features::World::WeatherCodes.data(),
                    static_cast<int>(Features::World::WeatherCodes.size())))
            {
            }

            if (ImGui::Button("Apply Weather"))
                static_cast<void>(service.QueueWeather(weatherIndex));
            ImGui::SameLine();
            if (ImGui::Button("Clear Weather Override"))
                static_cast<void>(service.QueueClearWeather());

            bool blackout = state.blackout;
            if (ImGui::Checkbox("Blackout", &blackout))
                static_cast<void>(service.QueueBlackout(blackout));

            ImGui::SeparatorText("Population Density");
            float pedDensity = state.pedDensity;
            float scenarioDensity = state.scenarioPedDensity;
            float vehicleDensity = state.vehicleDensity;
            float randomVehicleDensity = state.randomVehicleDensity;
            float parkedDensity = state.parkedVehicleDensity;

            if (ImGui::SliderFloat("Ambient Peds", &pedDensity, 0.0f, 1.0f, "%.2f"))
                service.SetPedDensity(pedDensity);
            if (ImGui::SliderFloat("Scenario Peds", &scenarioDensity, 0.0f, 1.0f, "%.2f"))
                service.SetScenarioPedDensity(scenarioDensity);
            if (ImGui::SliderFloat("Traffic", &vehicleDensity, 0.0f, 1.0f, "%.2f"))
                service.SetVehicleDensity(vehicleDensity);
            if (ImGui::SliderFloat("Random Traffic", &randomVehicleDensity, 0.0f, 1.0f, "%.2f"))
                service.SetRandomVehicleDensity(randomVehicleDensity);
            if (ImGui::SliderFloat("Parked Vehicles", &parkedDensity, 0.0f, 1.0f, "%.2f"))
                service.SetParkedVehicleDensity(parkedDensity);

            if (ImGui::Button("Normal Density"))
                service.ResetDensity();
            ImGui::SameLine();
            if (ImGui::Button("Sparse World"))
            {
                service.SetPedDensity(0.15f);
                service.SetScenarioPedDensity(0.15f);
                service.SetVehicleDensity(0.20f);
                service.SetRandomVehicleDensity(0.20f);
                service.SetParkedVehicleDensity(0.25f);
            }
            ImGui::SameLine();
            if (ImGui::Button("Empty World"))
            {
                service.SetPedDensity(0.0f);
                service.SetScenarioPedDensity(0.0f);
                service.SetVehicleDensity(0.0f);
                service.SetRandomVehicleDensity(0.0f);
                service.SetParkedVehicleDensity(0.0f);
            }

            ImGui::SeparatorText("Clear Nearby World");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::SliderFloat("Radius", &clearRadius, 5.0f, 250.0f, "%.0f m");
            if (ImGui::Button("Clear Peds"))
                static_cast<void>(service.QueueClearPeds(clearRadius));
            ImGui::SameLine();
            if (ImGui::Button("Clear Vehicles"))
                static_cast<void>(service.QueueClearVehicles(clearRadius));
            ImGui::SameLine();
            if (ImGui::Button("Clear Objects"))
                static_cast<void>(service.QueueClearObjects(clearRadius));
            if (ImGui::Button("Clear Ambient World"))
                static_cast<void>(service.QueueClearAmbient(clearRadius));

            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextDisabled(
                "Density loop: %s | World loop: %s",
                state.densityLoopRunning ? "ACTIVE" : "IDLE",
                state.worldLoopRunning ? "ACTIVE" : "IDLE");
            ImGui::TextWrapped("World status: %s", state.message.c_str());
        }

        void RenderRecovery() noexcept
        {
            auto& stats = Features::Player::PlayerStatsService::Get();
            const auto state = stats.Snapshot();
            const bool nativeReady = Game::GameRuntime::Get().NativeReady();

            static std::uint64_t lastRevision{};
            static int rank{1};
            static int rp{};
            static int kills{};
            static int deaths{};

            if (state.revision != lastRevision)
            {
                lastRevision = state.revision;
                if (state.readable)
                {
                    rank = state.rank;
                    rp = state.rp;
                    kills = state.kills;
                    deaths = state.deaths;
                }
            }

            ImGui::SeparatorText("Stats & Progression");
            if (state.readable)
            {
                ImGui::Text("Active character: MP%d", state.characterIndex);
                ImGui::Text("Observed K/D: %.2f", state.kdRatio);
            }

            ImGui::BeginDisabled(!stats.IsReady() || !nativeReady || state.pending);
            const int oldRank = rank;
            ImGui::InputInt("Rank##recovery", &rank, 1, 10);
            rank = std::clamp(rank, 1, 8000);
            if (rank != oldRank)
            {
                if (const auto requiredRp = Features::Player::PlayerStatsService::RpForRank(rank))
                    rp = *requiredRp;
            }
            ImGui::InputInt("RP##recovery", &rp, 100, 1000);
            ImGui::InputInt("Kills##recovery", &kills, 1, 10);
            ImGui::InputInt("Deaths##recovery", &deaths, 1, 10);
            rp = std::max(0, rp);
            kills = std::max(0, kills);
            deaths = std::max(0, deaths);

            if (ImGui::Button("Apply Recovery Stats"))
                static_cast<void>(stats.QueueApply(rank, rp, kills, deaths));
            ImGui::SameLine();
            if (ImGui::Button("Refresh Recovery Stats"))
                static_cast<void>(stats.QueueRefresh());
            ImGui::EndDisabled();

            ImGui::TextDisabled("%s", state.message.c_str());

            PlannedSection(
                "Businesses",
                "V1 business tools (Nightclub, Bunker, Special Cargo, Vehicle Cargo and more) are the next globals-backed Recovery pass.",
                "Recovery / Script Globals");
            PlannedSection(
                "Heists",
                "V1 heist setup and state editors will be ported only from verified Enhanced globals/script paths.",
                "Recovery / Script Runtime");
        }

        void RenderMisc() noexcept
        {
            auto& utilities = Features::Utility::UtilityService::Get();
            utilities.Maintain();
            const auto state = utilities.Snapshot();

            ImGui::SeparatorText("HUD / Overlay");
            bool showCoordinates = state.showCoordinates;
            bool showHeading = state.showHeading;
            bool showFps = state.showFps;
            bool showSessionInfo = state.showSessionInfo;
            bool disableCameraShake = state.disableCameraShake;

            if (ImGui::Checkbox("Coordinates Overlay", &showCoordinates))
            {
                utilities.SetShowCoordinates(showCoordinates);
                Config::SettingsService::Get().Update([showCoordinates](Config::MenuSettings& settings) {
                    settings.miscShowCoordinates = showCoordinates;
                });
            }
            if (ImGui::Checkbox("Heading Overlay", &showHeading))
            {
                utilities.SetShowHeading(showHeading);
                Config::SettingsService::Get().Update([showHeading](Config::MenuSettings& settings) {
                    settings.miscShowHeading = showHeading;
                });
            }
            if (ImGui::Checkbox("FPS Overlay", &showFps))
            {
                utilities.SetShowFps(showFps);
                Config::SettingsService::Get().Update([showFps](Config::MenuSettings& settings) {
                    settings.miscShowFps = showFps;
                });
            }
            if (ImGui::Checkbox("Session Info Overlay", &showSessionInfo))
            {
                utilities.SetShowSessionInfo(showSessionInfo);
                Config::SettingsService::Get().Update([showSessionInfo](Config::MenuSettings& settings) {
                    settings.miscShowSessionInfo = showSessionInfo;
                });
            }

            ImGui::SeparatorText("Camera");
            if (ImGui::Checkbox("Disable Camera Shake", &disableCameraShake))
            {
                utilities.SetDisableCameraShake(disableCameraShake);
                Config::SettingsService::Get().Update([disableCameraShake](Config::MenuSettings& settings) {
                    settings.miscDisableCameraShake = disableCameraShake;
                });
            }

            ImGui::SeparatorText("Live Player Position");
            if (state.positionReadable)
            {
                ImGui::Text(
                    "XYZ: %.3f, %.3f, %.3f",
                    state.position.x,
                    state.position.y,
                    state.position.z);
                ImGui::Text("Heading: %.2f", state.heading);
            }
            else
            {
                ImGui::TextDisabled("Enable Coordinates or Heading to sample live position.");
            }

            ImGui::TextDisabled("Overlay settings are saved in Tutones-Menu-V2.ini.");
        }

        void RenderSettings() noexcept
        {
            auto& theme = MenuTheme::Get();
            auto& gameRuntime = Game::GameRuntime::Get();
            auto& backend = Backend::BackendHub::Get();

            ImGui::SeparatorText("Appearance");
            if (ImGui::SliderFloat("UI scale", &theme.Scale(), 0.85f, 1.35f, "%.2fx"))
            {
                const float value = theme.Scale();
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.scale = value;
                });
            }
            if (ImGui::SliderFloat("Window opacity", &theme.Opacity(), 0.70f, 1.00f, "%.2f"))
            {
                const float value = theme.Opacity();
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.opacity = value;
                });
            }
            if (ImGui::ColorEdit3("Accent", theme.AccentColor(), ImGuiColorEditFlags_NoInputs))
            {
                const auto* accent = theme.AccentColor();
                const float r = accent[0];
                const float g = accent[1];
                const float b = accent[2];
                Config::SettingsService::Get().Update([r, g, b](Config::MenuSettings& settings) {
                    settings.accent[0] = r;
                    settings.accent[1] = g;
                    settings.accent[2] = b;
                    settings.accent[3] = 1.0f;
                });
            }
            if (ImGui::Checkbox("Show status bar", &theme.ShowStatusBar()))
            {
                const bool value = theme.ShowStatusBar();
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.showStatusBar = value;
                });
            }

            if (ImGui::Button("Reset appearance"))
            {
                theme.Reset();
                const auto* accent = theme.AccentColor();
                const float opacity = theme.Opacity();
                const float scale = theme.Scale();
                const bool showStatusBar = theme.ShowStatusBar();
                const float r = accent[0];
                const float g = accent[1];
                const float b = accent[2];
                Config::SettingsService::Get().Update(
                    [opacity, scale, showStatusBar, r, g, b](Config::MenuSettings& settings) {
                        settings.opacity = opacity;
                        settings.scale = scale;
                        settings.showStatusBar = showStatusBar;
                        settings.accent = {r, g, b, 1.0f};
                    });
            }

            ImGui::SameLine();
            if (ImGui::Button("Save Settings Now"))
                static_cast<void>(Config::SettingsService::Get().SaveNow());

            const auto settingsPath = Config::SettingsService::Get().Path().string();
            ImGui::TextDisabled("Config: %s", settingsPath.c_str());

            ImGui::Spacing();
            ImGui::SeparatorText("Runtime");
            ImGui::BulletText("Menu toggle: Insert or F4");
            ImGui::BulletText("Input capture: Win32 / ImGui");
            ImGui::BulletText("Renderer: DirectX 12");
            ImGui::BulletText("Feature work on Present: disabled");
            ImGui::BulletText(
                "Native runtime: %s",
                Game::GameRuntime::NativeStateName(gameRuntime.NativeState()));
            ImGui::BulletText(
                "Backend command bus: %zu / %zu pending",
                backend.PendingCount(),
                Backend::BackendHub::CommandCapacity);
            ImGui::TextDisabled("Native target reference: GTA V Enhanced 1.73 / b1158.13 (pre-update checkpoint)");

            if (!gameRuntime.NativeRuntimeAvailable())
            {
                ImGui::TextWrapped(
                    "Compatibility gate is closed. DX12/UI may continue, but the GTA script scheduler hook and native feature execution remain disabled.");
            }
            else if (!gameRuntime.NativeReady())
            {
                ImGui::TextWrapped(
                    "Native prerequisites resolved, but execution is not considered ready until the scheduler, handler cache, and PLAYER_PED_ID canary pass.");
            }
        }
    }

    void PageRenderer::Render(MenuPage page) noexcept
    {
        switch (page)
        {
        case MenuPage::Self: RenderSelf(); break;
        case MenuPage::Weapons: RenderWeapons(); break;
        case MenuPage::Vehicle: RenderVehicle(); break;
        case MenuPage::Online: RenderOnline(); break;
        case MenuPage::Teleport: RenderTeleport(); break;
        case MenuPage::World: RenderWorld(); break;
        case MenuPage::Recovery: RenderRecovery(); break;
        case MenuPage::Misc: RenderMisc(); break;
        case MenuPage::Settings: RenderSettings(); break;
        }
    }
}
