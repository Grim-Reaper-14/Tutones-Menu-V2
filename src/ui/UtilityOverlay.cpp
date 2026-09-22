#include "UtilityOverlay.hpp"

#include "../features/online/OnlineStatusService.hpp"
#include "../features/utility/UtilityService.hpp"

#include <imgui.h>

namespace TutonesV2::UI
{
    bool UtilityOverlay::AnyVisible() noexcept
    {
        return Features::Utility::UtilityService::Get().AnyOverlayVisible();
    }

    void UtilityOverlay::Render() noexcept
    {
        auto& utilities = Features::Utility::UtilityService::Get();
        if (!utilities.AnyOverlayVisible())
            return;

        utilities.Maintain();
        const auto state = utilities.Snapshot();

        if (state.showSessionInfo)
            Features::Online::OnlineStatusService::Get().RequestRefresh();
        const auto online = Features::Online::OnlineStatusService::Get().Snapshot();

        ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs;

        if (!ImGui::Begin("##tutones_v2_utility_overlay", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("TUTONES V2");

        if (state.showCoordinates)
        {
            if (state.positionReadable)
                ImGui::Text("XYZ  %.2f  %.2f  %.2f", state.position.x, state.position.y, state.position.z);
            else
                ImGui::TextDisabled("XYZ  waiting...");
        }

        if (state.showHeading)
            ImGui::Text("Heading  %.1f", state.heading);

        if (state.showFps)
            ImGui::Text("FPS  %.0f", ImGui::GetIO().Framerate);

        if (state.showSessionInfo)
        {
            ImGui::Separator();
            ImGui::Text(
                "Session  %s",
                online.sessionStarted ? "ONLINE" : "OFFLINE");
            ImGui::Text("Local player  %d", online.localPlayer);
            ImGui::Text(
                "Freemode  %s",
                online.freemodeReady ? "READY" : "WAITING");
        }

        ImGui::End();
    }
}
