#include "PageRenderer.hpp"

#include "MenuTheme.hpp"
#include "../backend/BackendHub.hpp"
#include "../game/GameRuntime.hpp"

#include <imgui.h>

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
            PlannedSection("Player", "Health, armor, wanted-state, and player-state controls will live here.", "PlayerService");
            PlannedSection("Movement", "Movement modifiers and traversal controls will stay isolated from the render thread.", "PlayerService / GameScheduler");
            PlannedSection("Utilities", "Lightweight player utilities will dispatch work through backend services instead of Present.", "BackendHub");
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
