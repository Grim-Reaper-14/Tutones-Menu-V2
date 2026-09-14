#include "Menu.hpp"

#include <imgui.h>

namespace TutonesV2::UI
{
    Menu& Menu::Get() noexcept
    {
        static Menu instance;
        return instance;
    }

    void Menu::Toggle() noexcept
    {
        m_Open.store(!m_Open.load());
    }

    void Menu::SetOpen(bool open) noexcept
    {
        m_Open.store(open);
    }

    bool Menu::IsOpen() const noexcept
    {
        return m_Open.load();
    }

    void Menu::Render() noexcept
    {
        if (!IsOpen())
            return;

        ImGui::SetNextWindowSize(ImVec2(720.0f, 460.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(80.0f, 80.0f), ImGuiCond_FirstUseEver);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        if (!ImGui::Begin("Tutones Menu V2", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("Tutones Menu V2");
        ImGui::SameLine();
        ImGui::TextDisabled("DX12 stability shell");
        ImGui::Separator();

        ImGui::BeginChild("##navigation", ImVec2(180.0f, 0.0f), true);
        ImGui::TextDisabled("MAIN");
        ImGui::Spacing();
        ImGui::Selectable("Self", true);
        ImGui::Selectable("Vehicle", false);
        ImGui::Selectable("Teleport", false);
        ImGui::Selectable("World", false);
        ImGui::Selectable("Recovery", false);
        ImGui::Selectable("Settings", false);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), true);
        ImGui::TextUnformatted("Self");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Visible-menu verification build. No game features, natives, globals, "
            "pattern scans, or backend feature ticks are running from this window.");
        ImGui::Spacing();
        ImGui::BulletText("DX12 overlay: active");
        ImGui::BulletText("Menu toggle: F5");
        ImGui::BulletText("Render path: UI only");
        ImGui::BulletText("Feature calls: disabled for stability test");
        ImGui::Spacing();
        ImGui::TextDisabled("Press F5 to close the menu.");
        ImGui::EndChild();

        ImGui::End();
    }
}
