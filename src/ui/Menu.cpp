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

        const auto pageName = [](Page page) noexcept -> const char*
        {
            switch (page)
            {
            case Page::Self: return "Self";
            case Page::Vehicle: return "Vehicle";
            case Page::Teleport: return "Teleport";
            case Page::World: return "World";
            case Page::Recovery: return "Recovery";
            case Page::Settings: return "Settings";
            }
            return "Self";
        };

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

        if (ImGui::Selectable("Self", m_Page == Page::Self))
            m_Page = Page::Self;
        if (ImGui::Selectable("Vehicle", m_Page == Page::Vehicle))
            m_Page = Page::Vehicle;
        if (ImGui::Selectable("Teleport", m_Page == Page::Teleport))
            m_Page = Page::Teleport;
        if (ImGui::Selectable("World", m_Page == Page::World))
            m_Page = Page::World;
        if (ImGui::Selectable("Recovery", m_Page == Page::Recovery))
            m_Page = Page::Recovery;
        if (ImGui::Selectable("Settings", m_Page == Page::Settings))
            m_Page = Page::Settings;

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), true);
        ImGui::TextUnformatted(pageName(m_Page));
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Visible-menu verification build. Navigation is active, but no game features, natives, globals, "
            "pattern scans, or backend feature ticks are running from this window.");
        ImGui::Spacing();
        ImGui::BulletText("DX12 overlay: active");
        ImGui::BulletText("Mouse input: captured while menu is open");
        ImGui::BulletText("Selected page: %s", pageName(m_Page));
        ImGui::BulletText("Feature calls: disabled for stability test");
        ImGui::Spacing();
        ImGui::TextDisabled("Press F5 to close the menu.");
        ImGui::EndChild();

        ImGui::End();
    }
}
