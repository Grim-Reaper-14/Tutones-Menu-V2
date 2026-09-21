#include "PageRenderer.hpp"

#include "MenuTheme.hpp"
#include "../backend/BackendHub.hpp"
#include "../features/player/PlayerService.hpp"
#include "../features/player/PlayerStatsService.hpp"
#include "../features/player/SelfOnlineService.hpp"
#include "../game/GameRuntime.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace TutonesV2::UI
{
    namespace
    {
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
            static int health{200};
            static int armor{100};
            static int wantedLevel{};
            static char modelName[64]{"mp_m_freemode_01"};
            static int component{};
            static int drawable{};
            static int texture{};
            static int palette{};

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
                ImGui::InputInt("Health", &health, 10, 50);
                ImGui::SameLine();
                if (ImGui::Button("Set Health"))
                    status = player.QueueSetHealth(health) ? "Set Health queued" : "Set Health rejected";
                ImGui::SameLine();
                if (ImGui::Button("Full Heal"))
                    status = player.QueueHeal() ? "Full Heal queued" : "Full Heal rejected";

                ImGui::SetNextItemWidth(130.0f);
                ImGui::InputInt("Armor", &armor, 10, 25);
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
                ImGui::InputInt("Wanted Level", &wantedLevel, 1, 1);
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
                ImGui::InputText("Ped Model", modelName, sizeof(modelName));
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
                ImGui::InputInt("Component", &component, 1, 1);
                ImGui::SetNextItemWidth(100.0f);
                ImGui::InputInt("Drawable", &drawable, 1, 5);
                ImGui::SetNextItemWidth(100.0f);
                ImGui::InputInt("Texture", &texture, 1, 5);
                ImGui::SetNextItemWidth(100.0f);
                ImGui::InputInt("Palette", &palette, 1, 1);

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

        void RenderVehicle() noexcept
        {
            PlannedSection("Current Vehicle", "Actions for the current or last vehicle will be grouped here.", "VehicleService");
            PlannedSection("Spawner", "Vehicle search, categories, favorites, and spawn requests will use backend commands.", "VehicleService / NativeRuntime");
            PlannedSection("Customization", "Vehicle appearance and handling controls will remain separate from menu rendering.", "VehicleService");
        }

        void RenderTeleport() noexcept
        {
            PlannedSection("Quick Teleports", "Common locations will be exposed as simple backend requests.", "TeleportService");
            PlannedSection("Waypoint & Objective", "Waypoint and objective destinations will be resolved outside the DX12 hot path.", "TeleportService / NativeRuntime");
            PlannedSection("Saved Locations", "Named locations will be stored by the configuration layer and dispatched on demand.", "TeleportService / Config");

            ImGui::SeparatorText("Compatibility Lock");
            ImGui::TextWrapped(
                "Teleport execution is intentionally locked while the post-update GTA native runtime is unverified. The UI and backend command path can be prepared without calling game natives.");
            ImGui::BeginDisabled();
            ImGui::Button("Teleport to Waypoint (prepared)");
            ImGui::SameLine();
            ImGui::Button("Teleport to Objective (prepared)");
            ImGui::EndDisabled();
        }

        void RenderWorld() noexcept
        {
            PlannedSection("Time & Weather", "World-state controls will be grouped here when the game runtime is connected.", "WorldService");
            PlannedSection("Nearby World", "Entity and world utilities will use scheduled backend work rather than per-frame scans.", "WorldService / GameScheduler");
            PlannedSection("Services", "Request-service tools can be added here once native and script systems are proven stable.", "WorldService / ScriptRuntime");
        }

        void RenderRecovery() noexcept
        {
            PlannedSection("Stats & Unlocks", "Stats, unlocks, and progression tools will remain grouped under Recovery.", "RecoveryService");
            PlannedSection("Businesses", "Business helpers will be wired only after globals and script access are validated.", "RecoveryService / ScriptRuntime");
            PlannedSection("Heists", "Heist-state controls will use explicit backend requests with validation before writes.", "RecoveryService / ScriptRuntime");
        }

        void RenderSettings() noexcept
        {
            auto& theme = MenuTheme::Get();
            auto& gameRuntime = Game::GameRuntime::Get();
            auto& backend = Backend::BackendHub::Get();

            ImGui::SeparatorText("Appearance");
            ImGui::SliderFloat("UI scale", &theme.Scale(), 0.85f, 1.35f, "%.2fx");
            ImGui::SliderFloat("Window opacity", &theme.Opacity(), 0.70f, 1.00f, "%.2f");
            ImGui::ColorEdit3("Accent", theme.AccentColor(), ImGuiColorEditFlags_NoInputs);
            ImGui::Checkbox("Show status bar", &theme.ShowStatusBar());

            if (ImGui::Button("Reset appearance"))
                theme.Reset();

            ImGui::Spacing();
            ImGui::SeparatorText("Runtime");
            ImGui::BulletText("Menu toggle: F5");
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
        case MenuPage::Vehicle: RenderVehicle(); break;
        case MenuPage::Teleport: RenderTeleport(); break;
        case MenuPage::World: RenderWorld(); break;
        case MenuPage::Recovery: RenderRecovery(); break;
        case MenuPage::Settings: RenderSettings(); break;
        }
    }
}
