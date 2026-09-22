#include "Menu.hpp"

#include "MenuTheme.hpp"
#include "PageRenderer.hpp"
#include "../config/SettingsService.hpp"

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

    void Menu::SetPage(MenuPage page) noexcept
    {
        m_Page = page;
    }

    MenuPage Menu::CurrentPage() const noexcept
    {
        return m_Page;
    }

    void Menu::Render() noexcept
    {
        if (!IsOpen())
            return;

        auto& theme = MenuTheme::Get();
        theme.Apply();

        ImGui::SetNextWindowSize(ImVec2(780.0f, 500.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(80.0f, 80.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(theme.Opacity());

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        if (!ImGui::Begin("Tutones Menu V2", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("TUTONES MENU V2");
        ImGui::SameLine();
        ImGui::TextDisabled("GTA V Enhanced / DX12");
        ImGui::Separator();

        ImGui::BeginChild("##navigation", ImVec2(190.0f, 0.0f), true);
        ImGui::TextDisabled("MAIN");
        ImGui::Spacing();

        for (const auto& descriptor : MenuPages)
        {
            if (ImGui::Selectable(descriptor.Label, m_Page == descriptor.Page))
            {
                m_Page = descriptor.Page;
                Config::SettingsService::Get().Update([this](Config::MenuSettings& settings) {
                    settings.selectedPage = static_cast<int>(m_Page);
                });
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Insert / F4  Toggle menu");
        ImGui::TextDisabled("Mouse  UI control");
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), true);
        ImGui::TextUnformatted(MenuPageName(m_Page));
        ImGui::SameLine();
        ImGui::TextDisabled("frontend");
        ImGui::Separator();

        const float footerReserve = theme.ShowStatusBar() ? 34.0f : 0.0f;
        ImGui::BeginChild("##page_body", ImVec2(0.0f, -footerReserve), false);
        PageRenderer::Render(m_Page);
        ImGui::EndChild();

        if (theme.ShowStatusBar())
        {
            ImGui::Separator();
            ImGui::TextDisabled("DX12 stable  |  input captured  |  %.0f FPS", ImGui::GetIO().Framerate);
            ImGui::SameLine();
            ImGui::TextDisabled("Insert / F4 close");
        }

        ImGui::EndChild();
        ImGui::End();
    }
}
