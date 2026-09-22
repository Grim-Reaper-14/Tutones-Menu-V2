#include "PageRenderer.hpp"

#include "MenuTheme.hpp"
#include "VehicleThumbnailCache.hpp"
#include "../backend/BackendHub.hpp"
#include "../config/SettingsService.hpp"
#include "../features/player/PlayerService.hpp"
#include "../features/player/PlayerStatsService.hpp"
#include "../features/player/SelfOnlineService.hpp"
#include "../features/protection/ProtectionService.hpp"
#include "../features/online/OnlineStatusService.hpp"
#include "../features/utility/UtilityService.hpp"
#include "../features/vehicle/VehicleService.hpp"
#include "../features/weapon/WeaponService.hpp"
#include "../features/world/TeleportService.hpp"
#include "../features/world/WorldService.hpp"
#include "../game/GameRuntime.hpp"
#include "../game/vehicle/VehicleCatalogs.hpp"
#include "../game/vehicle/VehicleModels.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

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
                {
                    status = player.SetGodMode(godMode) ? "God Mode updated" : "God Mode request rejected";
                    Config::SettingsService::Get().Update([godMode](Config::MenuSettings& settings) { settings.selfGodMode = godMode; });
                }
                if (ImGui::Checkbox("Bulletproof / Semi-God", &bulletproof))
                {
                    status = player.SetBulletproof(bulletproof) ? "Bulletproof updated" : "Bulletproof request rejected";
                    Config::SettingsService::Get().Update([bulletproof](Config::MenuSettings& settings) { settings.selfBulletproof = bulletproof; });
                }
                if (ImGui::Checkbox("Invisible", &invisible))
                {
                    status = player.SetInvisible(invisible) ? "Invisible updated" : "Invisible request rejected";
                    Config::SettingsService::Get().Update([invisible](Config::MenuSettings& settings) { settings.selfInvisible = invisible; });
                }
                if (ImGui::Checkbox("Disable Critical Hits", &disableCriticalHits))
                {
                    status = player.SetDisableCriticalHits(disableCriticalHits) ? "Critical hits updated" : "Critical-hit request rejected";
                    Config::SettingsService::Get().Update([disableCriticalHits](Config::MenuSettings& settings) { settings.selfDisableCriticalHits = disableCriticalHits; });
                }
                if (ImGui::Checkbox("Keep Player Clean", &keepClean))
                {
                    status = player.SetKeepPlayerClean(keepClean) ? "Keep Player Clean updated" : "Keep Player Clean request rejected";
                    Config::SettingsService::Get().Update([keepClean](Config::MenuSettings& settings) { settings.selfKeepClean = keepClean; });
                }
                if (ImGui::Checkbox("No Ragdoll", &noRagdoll))
                {
                    status = player.SetNoRagdoll(noRagdoll) ? "No Ragdoll updated" : "No Ragdoll request rejected";
                    Config::SettingsService::Get().Update([noRagdoll](Config::MenuSettings& settings) { settings.selfNoRagdoll = noRagdoll; });
                }
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
                {
                    status = player.SetNeverWanted(neverWanted) ? "Never Wanted updated" : "Never Wanted request rejected";
                    Config::SettingsService::Get().Update([neverWanted](Config::MenuSettings& settings) { settings.selfNeverWanted = neverWanted; });
                }
                if (ImGui::Checkbox("Police Ignore", &policeIgnore))
                {
                    status = player.SetPoliceIgnore(policeIgnore) ? "Police Ignore updated" : "Police Ignore request rejected";
                    Config::SettingsService::Get().Update([policeIgnore](Config::MenuSettings& settings) { settings.selfPoliceIgnore = policeIgnore; });
                }
                if (ImGui::Checkbox("Everyone Ignore", &everyoneIgnore))
                {
                    status = player.SetEveryoneIgnore(everyoneIgnore) ? "Everyone Ignore updated" : "Everyone Ignore request rejected";
                    Config::SettingsService::Get().Update([everyoneIgnore](Config::MenuSettings& settings) { settings.selfEveryoneIgnore = everyoneIgnore; });
                }

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
                {
                    status = player.SetSuperJump(superJump) ? "Super Jump updated" : "Super Jump request rejected";
                    Config::SettingsService::Get().Update([superJump](Config::MenuSettings& settings) { settings.selfSuperJump = superJump; });
                }
                if (ImGui::Checkbox("Infinite Stamina", &infiniteStamina))
                {
                    status = player.SetInfiniteStamina(infiniteStamina) ? "Infinite Stamina updated" : "Infinite Stamina request rejected";
                    Config::SettingsService::Get().Update([infiniteStamina](Config::MenuSettings& settings) { settings.selfInfiniteStamina = infiniteStamina; });
                }
                if (ImGui::Checkbox("Stand On Vehicles", &standOnVehicles))
                {
                    status = player.SetStandOnVehicles(standOnVehicles) ? "Stand On Vehicles updated" : "Stand On Vehicles request rejected";
                    Config::SettingsService::Get().Update([standOnVehicles](Config::MenuSettings& settings) { settings.selfStandOnVehicles = standOnVehicles; });
                }
                if (ImGui::Checkbox("Disable Action Mode", &disableActionMode))
                {
                    status = player.SetDisableActionMode(disableActionMode) ? "Disable Action Mode updated" : "Disable Action Mode rejected";
                    Config::SettingsService::Get().Update([disableActionMode](Config::MenuSettings& settings) { settings.selfDisableActionMode = disableActionMode; });
                }
                if (ImGui::Checkbox("Aqua Lungs", &aquaLungs))
                {
                    status = player.SetAquaLungs(aquaLungs) ? "Aqua Lungs updated" : "Aqua Lungs request rejected";
                    Config::SettingsService::Get().Update([aquaLungs](Config::MenuSettings& settings) { settings.selfAquaLungs = aquaLungs; });
                }
                if (ImGui::Checkbox("Infinite Oxygen", &infiniteOxygen))
                {
                    status = player.SetInfiniteOxygen(infiniteOxygen) ? "Infinite Oxygen updated" : "Infinite Oxygen request rejected";
                    Config::SettingsService::Get().Update([infiniteOxygen](Config::MenuSettings& settings) { settings.selfInfiniteOxygen = infiniteOxygen; });
                }

                if (ImGui::SliderFloat("Run / Sprint", &runMultiplier, 1.0f, 1.49f, "%.2fx"))
                {
                    status = player.SetRunMultiplier(runMultiplier) ? "Run multiplier updated" : "Run multiplier rejected";
                    Config::SettingsService::Get().Update([runMultiplier](Config::MenuSettings& settings) { settings.selfRunMultiplier = runMultiplier; });
                }
                if (ImGui::SliderFloat("Swim", &swimMultiplier, 1.0f, 1.49f, "%.2fx"))
                {
                    status = player.SetSwimMultiplier(swimMultiplier) ? "Swim multiplier updated" : "Swim multiplier rejected";
                    Config::SettingsService::Get().Update([swimMultiplier](Config::MenuSettings& settings) { settings.selfSwimMultiplier = swimMultiplier; });
                }
            }

            if (ImGui::CollapsingHeader("Utilities"))
            {
                bool infiniteParachutes = player.InfiniteParachutes();
                bool mobileRadio = player.MobileRadio();

                if (ImGui::Checkbox("Infinite Parachutes", &infiniteParachutes))
                {
                    status = player.SetInfiniteParachutes(infiniteParachutes) ? "Infinite Parachutes updated" : "Infinite Parachutes rejected";
                    Config::SettingsService::Get().Update([infiniteParachutes](Config::MenuSettings& settings) { settings.selfInfiniteParachutes = infiniteParachutes; });
                }
                if (ImGui::Checkbox("Mobile Radio", &mobileRadio))
                {
                    status = player.SetMobileRadio(mobileRadio) ? "Mobile Radio updated" : "Mobile Radio rejected";
                    Config::SettingsService::Get().Update([mobileRadio](Config::MenuSettings& settings) { settings.selfMobileRadio = mobileRadio; });
                }
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
                    const int savedMode = static_cast<int>(requested);
                    Config::SettingsService::Get().Update([savedMode](Config::MenuSettings& settings) { settings.selfRadarMode = savedMode; });
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
            {
                status = service.SetInfiniteAmmo(infiniteAmmo) ? "Infinite Ammo updated" : "Infinite Ammo rejected";
                Config::SettingsService::Get().Update([infiniteAmmo](Config::MenuSettings& settings) { settings.weaponInfiniteAmmo = infiniteAmmo; });
            }
            if (ImGui::Checkbox("Infinite Clip", &infiniteClip))
            {
                status = service.SetInfiniteClip(infiniteClip) ? "Infinite Clip updated" : "Infinite Clip rejected";
                Config::SettingsService::Get().Update([infiniteClip](Config::MenuSettings& settings) { settings.weaponInfiniteClip = infiniteClip; });
            }
            if (ImGui::Checkbox("Explosive Ammo", &explosiveAmmo))
            {
                status = service.SetExplosiveAmmo(explosiveAmmo) ? "Explosive Ammo updated" : "Explosive Ammo rejected";
                Config::SettingsService::Get().Update([explosiveAmmo](Config::MenuSettings& settings) { settings.weaponExplosiveAmmo = explosiveAmmo; });
            }

            ImGui::SeparatorText("Aim Assist");
            ImGui::TextDisabled(
                "Aimbot: %s | Head: %s | Drivers: %s",
                service.AimbotSupported() ? "READY" : "UNAVAILABLE",
                service.AimForHeadSupported() ? "READY" : "UNAVAILABLE",
                service.TargetDriversSupported() ? "READY" : "UNAVAILABLE");

            ImGui::BeginDisabled(!service.AimbotSupported());
            if (ImGui::Checkbox("Aimbot", &aimbot))
            {
                status = service.SetAimbot(aimbot) ? "Aimbot updated" : "Aimbot rejected";
                Config::SettingsService::Get().Update([aimbot](Config::MenuSettings& settings) { settings.weaponAimbot = aimbot; });
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!aimbot || !service.AimForHeadSupported());
            if (ImGui::Checkbox("Aim For Head", &aimForHead))
            {
                status = service.SetAimForHead(aimForHead) ? "Aim For Head updated" : "Aim For Head rejected";
                Config::SettingsService::Get().Update([aimForHead](Config::MenuSettings& settings) { settings.weaponAimForHead = aimForHead; });
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!aimbot || !service.TargetDriversSupported());
            if (ImGui::Checkbox("Target Drivers", &targetDrivers))
            {
                status = service.SetTargetDrivers(targetDrivers) ? "Target Drivers updated" : "Target Drivers rejected";
                Config::SettingsService::Get().Update([targetDrivers](Config::MenuSettings& settings) { settings.weaponTargetDrivers = targetDrivers; });
            }
            ImGui::EndDisabled();

            if (ImGui::Checkbox("Laser Sight (Native)", &laserSight))
            {
                status = service.SetLaserSight(laserSight) ? "Native Laser Sight updated" : "Native Laser Sight rejected";
                Config::SettingsService::Get().Update([laserSight](Config::MenuSettings& settings) { settings.weaponLaserSight = laserSight; });
            }
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
            auto& thumbnails = VehicleThumbnailCache::Get();
            service.EnsureCatalog();

            const auto snapshot = service.Snapshot();
            const auto catalog = service.CatalogSnapshot();
            const auto artwork = thumbnails.SyncSnapshot();
            const bool nativeReady = Game::GameRuntime::Get().NativeReady();

            static bool settingsLoaded{};
            static char modelName[64]{"adder"};
            static char search[64]{};
            static bool enterVehicle = true;
            static bool networked = true;
            static bool spawnMaxed{};
            static bool cloneInside = true;
            static int classFilter{-1};
            static int selectedModel{-1};

            if (!settingsLoaded)
            {
                const auto saved = Config::SettingsService::Get().Snapshot();
                CopySettingText(modelName, saved.vehicleModel);
                enterVehicle = saved.vehicleEnterAfterSpawn;
                networked = saved.vehicleNetworked;
                spawnMaxed = saved.vehicleSpawnMaxed;
                cloneInside = saved.vehicleCloneInside;
                classFilter = saved.vehicleClassFilter;

                for (std::size_t i = 0; i < Game::VehicleCatalogs::VehicleModels.size(); ++i)
                {
                    if (std::string_view(Game::VehicleCatalogs::VehicleModels[i]) == std::string_view(modelName))
                    {
                        selectedModel = static_cast<int>(i);
                        break;
                    }
                }

                settingsLoaded = true;
            }

            auto searchMatches = [](std::string_view model, std::string_view display, std::string_view needle) {
                if (needle.empty())
                    return true;

                std::string haystack;
                haystack.reserve(model.size() + display.size() + 1);
                haystack.append(display);
                haystack.push_back(' ');
                haystack.append(model);

                std::string lowerNeedle(needle);
                std::transform(
                    haystack.begin(),
                    haystack.end(),
                    haystack.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                std::transform(
                    lowerNeedle.begin(),
                    lowerNeedle.end(),
                    lowerNeedle.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

                return haystack.find(lowerNeedle) != std::string::npos;
            };

            auto selectModel = [&](std::size_t index) {
                if (index >= Game::VehicleCatalogs::VehicleModels.size())
                    return;

                selectedModel = static_cast<int>(index);
                const char* model = Game::VehicleCatalogs::VehicleModels[index];
                CopySettingText(modelName, model);

                const std::string value(model);
                Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                    settings.vehicleModel = value;
                });
            };

            auto selectClass = [&](int nextClass) {
                classFilter = nextClass;
                const int savedClass = nextClass;
                Config::SettingsService::Get().Update([savedClass](Config::MenuSettings& settings) {
                    settings.vehicleClassFilter = savedClass;
                });

                if (nextClass >= 0)
                {
                    for (std::size_t i = 0; i < catalog.classes.size(); ++i)
                    {
                        if (catalog.classes[i] == nextClass)
                        {
                            selectModel(i);
                            break;
                        }
                    }
                }
            };

            ImGui::TextDisabled(
                "V1 catalog: %zu / %zu vehicles resolved",
                catalog.ready,
                catalog.total);

            if (artwork.running || artwork.completed)
            {
                ImGui::TextDisabled(
                    "Artwork: %zu cached/downloaded | %zu missing | %zu failed%s",
                    artwork.existing + artwork.downloaded,
                    artwork.missing,
                    artwork.failed,
                    artwork.running ? " | syncing..." : "");
            }

            if (ImGui::BeginTabBar("##vehicle_hub_tabs"))
            {
                if (ImGui::BeginTabItem("Spawner"))
                {
                    ImGui::SeparatorText("Vehicle Categories");

                    if (ImGui::Button("All Vehicles", ImVec2(118.0f, 28.0f)))
                        selectClass(-1);
                    ImGui::SameLine();
                    if (ImGui::Button("Refresh Photos", ImVec2(118.0f, 28.0f)))
                        thumbnails.Refresh();
                    ImGui::SameLine();
                    ImGui::TextDisabled(
                        classFilter < 0
                            ? "ALL"
                            : Game::VehicleCatalogs::VehicleClassNames[static_cast<std::size_t>(classFilter)]);

                    if (ImGui::BeginChild("##vehicle_category_cards", ImVec2(-1.0f, 176.0f), true))
                    {
                        constexpr int Columns = 4;
                        constexpr float CardHeight = 78.0f;
                        const float spacing = ImGui::GetStyle().ItemSpacing.x;
                        const float available = ImGui::GetContentRegionAvail().x;
                        const float cardWidth = std::max(92.0f, (available - spacing * (Columns - 1)) / Columns);

                        for (std::size_t i = 0; i < Game::VehicleCatalogs::VehicleClassNames.size(); ++i)
                        {
                            ImGui::PushID(static_cast<int>(i));
                            const ImVec2 startPos = ImGui::GetCursorScreenPos();
                            const bool selected = classFilter == static_cast<int>(i);
                            ImGui::InvisibleButton("##class_card", ImVec2(cardWidth, CardHeight));
                            const bool pressed = ImGui::IsItemClicked();

                            auto* draw = ImGui::GetWindowDrawList();
                            const ImU32 background = ImGui::GetColorU32(
                                selected ? ImGuiCol_ButtonActive
                                         : (ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
                            const ImU32 border = ImGui::GetColorU32(
                                selected ? ImGuiCol_CheckMark : ImGuiCol_Border);

                            draw->AddRectFilled(
                                startPos,
                                ImVec2(startPos.x + cardWidth, startPos.y + CardHeight),
                                background,
                                5.0f);
                            draw->AddRect(
                                startPos,
                                ImVec2(startPos.x + cardWidth, startPos.y + CardHeight),
                                border,
                                5.0f,
                                0,
                                selected ? 2.0f : 1.0f);

                            const auto thumb = thumbnails.ClassThumbnail(static_cast<int>(i));
                            constexpr float imageHeight = 52.0f;
                            if (thumb.Valid())
                            {
                                draw->AddImage(
                                    static_cast<ImTextureID>(thumb.textureId),
                                    ImVec2(startPos.x + 3.0f, startPos.y + 3.0f),
                                    ImVec2(startPos.x + cardWidth - 3.0f, startPos.y + imageHeight));
                            }
                            else
                            {
                                draw->AddText(
                                    ImVec2(startPos.x + 8.0f, startPos.y + 20.0f),
                                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                    "Loading photo...");
                            }

                            const char* label = Game::VehicleCatalogs::VehicleClassNames[i];
                            const ImVec2 labelSize = ImGui::CalcTextSize(label);
                            draw->AddText(
                                ImVec2(
                                    startPos.x + std::max(4.0f, (cardWidth - labelSize.x) * 0.5f),
                                    startPos.y + 57.0f),
                                ImGui::GetColorU32(ImGuiCol_Text),
                                label);

                            if (pressed)
                                selectClass(static_cast<int>(i));

                            ImGui::PopID();

                            if ((i % Columns) != Columns - 1
                                && i + 1 < Game::VehicleCatalogs::VehicleClassNames.size())
                            {
                                ImGui::SameLine();
                            }
                        }
                    }
                    ImGui::EndChild();

                    ImGui::SeparatorText(
                        classFilter < 0
                            ? "All Vehicles"
                            : Game::VehicleCatalogs::VehicleClassNames[static_cast<std::size_t>(classFilter)]);

                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextWithHint(
                        "##vehicle_search",
                        "Search vehicle name / model",
                        search,
                        sizeof(search));

                    std::vector<std::size_t> visibleModels;
                    visibleModels.reserve(Game::VehicleCatalogs::VehicleModels.size());
                    const std::string_view needle(search);

                    for (std::size_t i = 0; i < Game::VehicleCatalogs::VehicleModels.size(); ++i)
                    {
                        const int vehicleClass =
                            i < catalog.classes.size() ? catalog.classes[i] : -2;

                        if (classFilter >= 0 && vehicleClass != classFilter)
                            continue;

                        const char* model = Game::VehicleCatalogs::VehicleModels[i];
                        const std::string_view display =
                            i < catalog.displayNames.size() && !catalog.displayNames[i].empty()
                                ? std::string_view(catalog.displayNames[i])
                                : std::string_view(model);

                        if (searchMatches(model, display, needle))
                            visibleModels.push_back(i);
                    }

                    if (ImGui::BeginChild("##vehicle_photo_grid", ImVec2(-1.0f, 264.0f), true))
                    {
                        constexpr int Columns = 3;
                        constexpr float CardHeight = 118.0f;
                        constexpr float ImageHeight = 82.0f;
                        const float spacing = ImGui::GetStyle().ItemSpacing.x;
                        const float available = ImGui::GetContentRegionAvail().x;
                        const float cardWidth = std::max(126.0f, (available - spacing * (Columns - 1)) / Columns);
                        const int rowCount = static_cast<int>((visibleModels.size() + Columns - 1) / Columns);

                        ImGuiListClipper clipper;
                        clipper.Begin(rowCount, CardHeight + ImGui::GetStyle().ItemSpacing.y);

                        while (clipper.Step())
                        {
                            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                            {
                                for (int column = 0; column < Columns; ++column)
                                {
                                    const std::size_t visibleIndex =
                                        static_cast<std::size_t>(row * Columns + column);
                                    if (visibleIndex >= visibleModels.size())
                                        break;

                                    const std::size_t index = visibleModels[visibleIndex];
                                    const char* model = Game::VehicleCatalogs::VehicleModels[index];
                                    const char* display =
                                        index < catalog.displayNames.size() && !catalog.displayNames[index].empty()
                                            ? catalog.displayNames[index].c_str()
                                            : model;
                                    const int vehicleClass =
                                        index < catalog.classes.size() ? catalog.classes[index] : -1;

                                    ImGui::PushID(static_cast<int>(index));
                                    const ImVec2 startPos = ImGui::GetCursorScreenPos();
                                    const bool selected = selectedModel == static_cast<int>(index);
                                    ImGui::InvisibleButton(
                                        "##vehicle_photo_card",
                                        ImVec2(cardWidth, CardHeight));
                                    const bool pressed = ImGui::IsItemClicked();
                                    const bool hovered = ImGui::IsItemHovered();

                                    auto* draw = ImGui::GetWindowDrawList();
                                    const ImU32 background = ImGui::GetColorU32(
                                        selected ? ImGuiCol_ButtonActive
                                                 : (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
                                    const ImU32 border = ImGui::GetColorU32(
                                        selected ? ImGuiCol_CheckMark : ImGuiCol_Border);

                                    draw->AddRectFilled(
                                        startPos,
                                        ImVec2(startPos.x + cardWidth, startPos.y + CardHeight),
                                        background,
                                        5.0f);
                                    draw->AddRect(
                                        startPos,
                                        ImVec2(startPos.x + cardWidth, startPos.y + CardHeight),
                                        border,
                                        5.0f,
                                        0,
                                        selected ? 2.0f : 1.0f);

                                    const auto thumb = thumbnails.VehicleThumbnail(model, vehicleClass);
                                    if (thumb.Valid())
                                    {
                                        draw->AddImage(
                                            static_cast<ImTextureID>(thumb.textureId),
                                            ImVec2(startPos.x + 3.0f, startPos.y + 3.0f),
                                            ImVec2(startPos.x + cardWidth - 3.0f, startPos.y + ImageHeight));
                                    }
                                    else
                                    {
                                        const char* loading = "Loading vehicle photo...";
                                        const ImVec2 loadingSize = ImGui::CalcTextSize(loading);
                                        draw->AddText(
                                            ImVec2(
                                                startPos.x + std::max(4.0f, (cardWidth - loadingSize.x) * 0.5f),
                                                startPos.y + 34.0f),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                            loading);
                                    }

                                    std::string label(display);
                                    if (label.size() > 22)
                                        label = label.substr(0, 20) + "..";
                                    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                                    draw->AddText(
                                        ImVec2(
                                            startPos.x + std::max(4.0f, (cardWidth - textSize.x) * 0.5f),
                                            startPos.y + 88.0f),
                                        ImGui::GetColorU32(ImGuiCol_Text),
                                        label.c_str());

                                    const ImVec2 modelSize = ImGui::CalcTextSize(model);
                                    draw->AddText(
                                        ImVec2(
                                            startPos.x + std::max(4.0f, (cardWidth - modelSize.x) * 0.5f),
                                            startPos.y + 103.0f),
                                        ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                        model);

                                    if (pressed)
                                        selectModel(index);

                                    ImGui::PopID();

                                    if (column != Columns - 1 && visibleIndex + 1 < visibleModels.size())
                                        ImGui::SameLine();
                                }
                            }
                        }
                    }
                    ImGui::EndChild();

                    if (visibleModels.empty())
                    {
                        ImGui::TextDisabled(
                            classFilter >= 0 && catalog.loading
                                ? "Resolving vehicles for this category..."
                                : "No matching vehicles.");
                    }

                    ImGui::SeparatorText("Spawn Selected / Add-On Vehicle");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputTextWithHint(
                            "##spawn_model",
                            "Model name / add-on model",
                            modelName,
                            sizeof(modelName)))
                    {
                        selectedModel = -1;
                        const std::string value(modelName);
                        Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                            settings.vehicleModel = value;
                        });
                    }

                    if (ImGui::Checkbox("Spawn inside", &enterVehicle))
                    {
                        const bool value = enterVehicle;
                        Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                            settings.vehicleEnterAfterSpawn = value;
                        });
                    }

                    ImGui::SameLine();
                    if (ImGui::Checkbox("Spawn maxed", &spawnMaxed))
                    {
                        const bool value = spawnMaxed;
                        Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                            settings.vehicleSpawnMaxed = value;
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

                    ImGui::BeginDisabled(!service.IsReady() || !nativeReady || snapshot.busy);
                    if (ImGui::Button(
                            snapshot.busy ? "Loading Vehicle..." : "Spawn Vehicle",
                            ImVec2(-1.0f, 30.0f)))
                    {
                        static_cast<void>(
                            service.QueueSpawn(
                                modelName,
                                enterVehicle,
                                networked,
                                spawnMaxed));
                    }
                    ImGui::EndDisabled();

                    if (!artwork.message.empty())
                        ImGui::TextDisabled("Photo sync: %s", artwork.message.c_str());

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Current"))
                {
                    ImGui::BeginDisabled(!service.IsReady() || !nativeReady);

                    bool vehicleGodMode = service.VehicleGodMode();
                    bool keepVehicleClean = service.KeepVehicleClean();
                    bool hornBoost = service.HornBoost();

                    if (ImGui::Checkbox("Vehicle God Mode", &vehicleGodMode))
                    {
                        static_cast<void>(service.SetVehicleGodMode(vehicleGodMode));
                        Config::SettingsService::Get().Update([vehicleGodMode](Config::MenuSettings& settings) {
                            settings.vehicleGodMode = vehicleGodMode;
                        });
                    }

                    if (ImGui::Checkbox("Keep Vehicle Clean", &keepVehicleClean))
                    {
                        static_cast<void>(service.SetKeepVehicleClean(keepVehicleClean));
                        Config::SettingsService::Get().Update([keepVehicleClean](Config::MenuSettings& settings) {
                            settings.vehicleKeepClean = keepVehicleClean;
                        });
                    }

                    if (ImGui::Checkbox("Horn Boost", &hornBoost))
                    {
                        static_cast<void>(service.SetHornBoost(hornBoost));
                        Config::SettingsService::Get().Update([hornBoost](Config::MenuSettings& settings) {
                            settings.vehicleHornBoost = hornBoost;
                        });
                    }

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
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Clone"))
                {
                    if (ImGui::Checkbox("Enter cloned vehicle", &cloneInside))
                    {
                        const bool value = cloneInside;
                        Config::SettingsService::Get().Update([value](Config::MenuSettings& settings) {
                            settings.vehicleCloneInside = value;
                        });
                    }

                    ImGui::TextWrapped(
                        "Clone Current Vehicle copies the supported V1-style customization state: "
                        "paint/custom RGB, mod slots, wheel type, xenon/neon, tire smoke, burst state and drift tires.");

                    ImGui::BeginDisabled(!service.IsReady() || !nativeReady || snapshot.busy);
                    if (ImGui::Button("Clone Current Vehicle", ImVec2(-1.0f, 30.0f)))
                        static_cast<void>(service.QueueCloneCurrent(cloneInside, networked));
                    ImGui::EndDisabled();

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::Spacing();
            ImGui::TextWrapped("Vehicle status: %s", snapshot.message.c_str());
            if (snapshot.lastSpawnedVehicle)
                ImGui::TextDisabled("Last spawned handle: %d", snapshot.lastSpawnedVehicle);
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
            {
                service.SetAutoWaypoint(autoWaypoint);
                Config::SettingsService::Get().Update([autoWaypoint](Config::MenuSettings& settings) { settings.teleportAutoWaypoint = autoWaypoint; });
            }
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
                const auto requested = static_cast<Features::Player::RadarMode>(std::clamp(radarMode, 0, 2));
                static_cast<void>(selfOnline.SetMode(requested));
                const int savedMode = static_cast<int>(requested);
                Config::SettingsService::Get().Update([savedMode](Config::MenuSettings& settings) { settings.selfRadarMode = savedMode; });
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
            {
                static_cast<void>(service.QueueSetTime(hour, minute));
                const int savedHour = hour;
                const int savedMinute = minute;
                Config::SettingsService::Get().Update([savedHour, savedMinute](Config::MenuSettings& settings) {
                    settings.worldHour = savedHour;
                    settings.worldMinute = savedMinute;
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh Clock"))
                service.RequestClockSample();

            bool freezeClock = state.freezeClock;
            if (ImGui::Checkbox("Freeze Time", &freezeClock))
            {
                static_cast<void>(service.SetFreezeClock(freezeClock));
                Config::SettingsService::Get().Update([freezeClock](Config::MenuSettings& settings) { settings.worldFreezeClock = freezeClock; });
            }

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
            {
                static_cast<void>(service.QueueWeather(weatherIndex));
                const int savedWeather = weatherIndex;
                Config::SettingsService::Get().Update([savedWeather](Config::MenuSettings& settings) {
                    settings.worldWeatherOverride = true;
                    settings.worldWeatherIndex = savedWeather;
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Weather Override"))
            {
                static_cast<void>(service.QueueClearWeather());
                Config::SettingsService::Get().Update([](Config::MenuSettings& settings) {
                    settings.worldWeatherOverride = false;
                });
            }

            bool blackout = state.blackout;
            if (ImGui::Checkbox("Blackout", &blackout))
            {
                static_cast<void>(service.QueueBlackout(blackout));
                Config::SettingsService::Get().Update([blackout](Config::MenuSettings& settings) { settings.worldBlackout = blackout; });
            }

            ImGui::SeparatorText("Population Density");
            float pedDensity = state.pedDensity;
            float scenarioDensity = state.scenarioPedDensity;
            float vehicleDensity = state.vehicleDensity;
            float randomVehicleDensity = state.randomVehicleDensity;
            float parkedDensity = state.parkedVehicleDensity;

            if (ImGui::SliderFloat("Ambient Peds", &pedDensity, 0.0f, 1.0f, "%.2f"))
            {
                service.SetPedDensity(pedDensity);
                Config::SettingsService::Get().Update([pedDensity](Config::MenuSettings& settings) { settings.worldPedDensity = pedDensity; });
            }
            if (ImGui::SliderFloat("Scenario Peds", &scenarioDensity, 0.0f, 1.0f, "%.2f"))
            {
                service.SetScenarioPedDensity(scenarioDensity);
                Config::SettingsService::Get().Update([scenarioDensity](Config::MenuSettings& settings) { settings.worldScenarioPedDensity = scenarioDensity; });
            }
            if (ImGui::SliderFloat("Traffic", &vehicleDensity, 0.0f, 1.0f, "%.2f"))
            {
                service.SetVehicleDensity(vehicleDensity);
                Config::SettingsService::Get().Update([vehicleDensity](Config::MenuSettings& settings) { settings.worldVehicleDensity = vehicleDensity; });
            }
            if (ImGui::SliderFloat("Random Traffic", &randomVehicleDensity, 0.0f, 1.0f, "%.2f"))
            {
                service.SetRandomVehicleDensity(randomVehicleDensity);
                Config::SettingsService::Get().Update([randomVehicleDensity](Config::MenuSettings& settings) { settings.worldRandomVehicleDensity = randomVehicleDensity; });
            }
            if (ImGui::SliderFloat("Parked Vehicles", &parkedDensity, 0.0f, 1.0f, "%.2f"))
            {
                service.SetParkedVehicleDensity(parkedDensity);
                Config::SettingsService::Get().Update([parkedDensity](Config::MenuSettings& settings) { settings.worldParkedVehicleDensity = parkedDensity; });
            }

            if (ImGui::Button("Normal Density"))
            {
                service.ResetDensity();
                Config::SettingsService::Get().Update([](Config::MenuSettings& settings) {
                    settings.worldPedDensity = 1.0f;
                    settings.worldScenarioPedDensity = 1.0f;
                    settings.worldVehicleDensity = 1.0f;
                    settings.worldRandomVehicleDensity = 1.0f;
                    settings.worldParkedVehicleDensity = 1.0f;
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Sparse World"))
            {
                service.SetPedDensity(0.15f);
                service.SetScenarioPedDensity(0.15f);
                service.SetVehicleDensity(0.20f);
                service.SetRandomVehicleDensity(0.20f);
                service.SetParkedVehicleDensity(0.25f);
                Config::SettingsService::Get().Update([](Config::MenuSettings& settings) {
                    settings.worldPedDensity = 0.15f;
                    settings.worldScenarioPedDensity = 0.15f;
                    settings.worldVehicleDensity = 0.20f;
                    settings.worldRandomVehicleDensity = 0.20f;
                    settings.worldParkedVehicleDensity = 0.25f;
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Empty World"))
            {
                service.SetPedDensity(0.0f);
                service.SetScenarioPedDensity(0.0f);
                service.SetVehicleDensity(0.0f);
                service.SetRandomVehicleDensity(0.0f);
                service.SetParkedVehicleDensity(0.0f);
                Config::SettingsService::Get().Update([](Config::MenuSettings& settings) {
                    settings.worldPedDensity = 0.0f;
                    settings.worldScenarioPedDensity = 0.0f;
                    settings.worldVehicleDensity = 0.0f;
                    settings.worldRandomVehicleDensity = 0.0f;
                    settings.worldParkedVehicleDensity = 0.0f;
                });
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

            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Teleport", ImGuiTreeNodeFlags_DefaultOpen))
                RenderTeleport();
        }

        void RenderBusiness() noexcept
        {
            ImGui::SeparatorText("V1 Business Hub");
            ImGui::TextWrapped("Nightclub, Special Cargo, Bunker, Motorcycle Club, Acid Lab, Hangar, Vehicle Cargo, Agency, Bail Office, Garment Factory and Money Fronts are being ported onto the V2 script/global runtime.");
            ImGui::Spacing();
            ImGui::BulletText("Business state backend: staging");
            ImGui::BulletText("Globals/script writes: readiness-gated");
            ImGui::BulletText("Vehicle Cargo actions: next backend group");
        }

        void RenderProtections() noexcept
        {
            auto& runtime = Features::Protection::ProtectionRuntime::Get();
            const auto state = runtime.Snapshot();

            ImGui::SeparatorText("Protection Runtime");
            ImGui::BulletText("Hook: %s", state.installed ? "ACTIVE" : "OFFLINE");
            ImGui::TextWrapped("%s", state.status.c_str());

            ImGui::SeparatorText("Packet Safety");
            bool blockMalformed = state.blockMalformed;
            bool blockKnownCrashes = state.blockKnownCrashes;
            bool blockForcedLeave = state.blockForcedLeave;
            if (ImGui::Checkbox("Block malformed packets", &blockMalformed))
                runtime.SetBlockMalformed(blockMalformed);
            if (ImGui::Checkbox("Block known crash payloads", &blockKnownCrashes))
                runtime.SetBlockKnownCrashes(blockKnownCrashes);
            if (ImGui::Checkbox("Block forced-leave traffic", &blockForcedLeave))
                runtime.SetBlockForcedLeave(blockForcedLeave);

            ImGui::SeparatorText("Network Events");
            bool blockSounds = state.blockSounds;
            bool blockExplosions = state.blockExplosions;
            bool blockFire = state.blockFire;
            bool blockWeaponDamage = state.blockWeaponDamage;
            bool blockRagdoll = state.blockRagdoll;
            bool blockClearTasks = state.blockClearTasks;
            bool blockPtfx = state.blockPtfx;

            if (ImGui::Checkbox("Block sound events", &blockSounds))
                runtime.SetBlockSounds(blockSounds);
            if (ImGui::Checkbox("Block explosion events", &blockExplosions))
                runtime.SetBlockExplosions(blockExplosions);
            if (ImGui::Checkbox("Block fire events", &blockFire))
                runtime.SetBlockFire(blockFire);
            if (ImGui::Checkbox("Block weapon-damage events", &blockWeaponDamage))
                runtime.SetBlockWeaponDamage(blockWeaponDamage);
            if (ImGui::Checkbox("Block ragdoll events", &blockRagdoll))
                runtime.SetBlockRagdoll(blockRagdoll);
            if (ImGui::Checkbox("Block clear-tasks events", &blockClearTasks))
                runtime.SetBlockClearTasks(blockClearTasks);
            if (ImGui::Checkbox("Block particle-FX events", &blockPtfx))
                runtime.SetBlockPtfx(blockPtfx);

            ImGui::SeparatorText("Script Events");
            bool blockScriptEvents = state.blockScriptEvents;
            bool blockMalformedScriptEvents = state.blockMalformedScriptEvents;
            if (ImGui::Checkbox("Block scripted events", &blockScriptEvents))
                runtime.SetBlockScriptEvents(blockScriptEvents);
            if (ImGui::Checkbox("Block malformed scripted events", &blockMalformedScriptEvents))
                runtime.SetBlockMalformedScriptEvents(blockMalformedScriptEvents);

            ImGui::SeparatorText("Live Counters");
            ImGui::Text("Packets inspected: %llu", static_cast<unsigned long long>(state.packetsInspected));
            ImGui::SameLine();
            ImGui::Text("blocked: %llu", static_cast<unsigned long long>(state.packetsBlocked));
            ImGui::Text("Events inspected: %llu", static_cast<unsigned long long>(state.eventsInspected));
            ImGui::SameLine();
            ImGui::Text("blocked: %llu", static_cast<unsigned long long>(state.eventsBlocked));
            ImGui::Text(
                "Forced-leave blocks: %llu | Crash blocks: %llu",
                static_cast<unsigned long long>(state.forcedLeaveAttemptsBlocked),
                static_cast<unsigned long long>(state.knownCrashAttemptsBlocked));
            ImGui::TextDisabled(
                "Last block: event %d | message %d | peer %u",
                state.lastBlockedEvent,
                state.lastBlockedMessageType,
                state.lastBlockedPeerId);

            if (ImGui::Button("Reset protection counters"))
                runtime.ResetCounters();
        }

        void RenderTools() noexcept
        {
            ImGui::SeparatorText("V1 Tools");
            ImGui::TextWrapped("Workshop, Vehicle & Camera, World Tools and Diagnostics are being consolidated here using the V2 native registry and scheduler.");
            ImGui::Spacing();
            ImGui::BulletText("Weapon components / tints");
            ImGui::BulletText("Props / outfits / animations");
            ImGui::BulletText("Freecam / advanced vehicle natives");
            ImGui::BulletText("Blips / PTFX / bodyguards / IPL / interiors");
            ImGui::BulletText("Native / tunable / script diagnostics");
        }

        void RenderHeists() noexcept
        {
            ImGui::SeparatorText("V1 Heist Hub");
            ImGui::TextWrapped("Apartment, Doomsday, Diamond Casino, Cayo Perico, Auto Shop, Salvage Yard and related Enhanced utilities are being moved onto the V2 script/global layer.");
            ImGui::Spacing();
            ImGui::BulletText("Apartment Heists");
            ImGui::BulletText("Doomsday Heist");
            ImGui::BulletText("Diamond Casino Heist");
            ImGui::BulletText("Cayo Perico");
            ImGui::BulletText("Auto Shop Contracts / Exotic Exports");
            ImGui::BulletText("Salvage Yard / Tow Truck");
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
        case MenuPage::World: RenderWorld(); break;
        case MenuPage::Business: RenderBusiness(); break;
        case MenuPage::Recovery: RenderRecovery(); break;
        case MenuPage::Protections: RenderProtections(); break;
        case MenuPage::Settings: RenderSettings(); break;
        case MenuPage::Misc: RenderMisc(); break;
        case MenuPage::Tools: RenderTools(); break;
        case MenuPage::Heists: RenderHeists(); break;
        }
    }
}
