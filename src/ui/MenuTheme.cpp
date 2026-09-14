#include "MenuTheme.hpp"

#include <imgui.h>

namespace TutonesV2::UI
{
    MenuTheme& MenuTheme::Get() noexcept
    {
        static MenuTheme instance;
        return instance;
    }

    void MenuTheme::Apply() noexcept
    {
        auto& io = ImGui::GetIO();
        io.FontGlobalScale = m_Scale;

        auto& style = ImGui::GetStyle();
        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.GrabRounding = 5.0f;
        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.FramePadding = ImVec2(10.0f, 6.0f);
        style.ItemSpacing = ImVec2(8.0f, 8.0f);

        const ImVec4 accent(m_Accent[0], m_Accent[1], m_Accent[2], 1.0f);
        const ImVec4 accentSoft(m_Accent[0] * 0.45f, m_Accent[1] * 0.45f, m_Accent[2] * 0.45f, 1.0f);
        const ImVec4 accentHover(
            m_Accent[0] + (1.0f - m_Accent[0]) * 0.15f,
            m_Accent[1] + (1.0f - m_Accent[1]) * 0.15f,
            m_Accent[2] + (1.0f - m_Accent[2]) * 0.15f,
            1.0f);

        style.Colors[ImGuiCol_CheckMark] = accent;
        style.Colors[ImGuiCol_SliderGrab] = accent;
        style.Colors[ImGuiCol_SliderGrabActive] = accentHover;
        style.Colors[ImGuiCol_Header] = accentSoft;
        style.Colors[ImGuiCol_HeaderHovered] = accent;
        style.Colors[ImGuiCol_HeaderActive] = accentHover;
        style.Colors[ImGuiCol_ButtonHovered] = accentSoft;
        style.Colors[ImGuiCol_ButtonActive] = accent;
    }

    void MenuTheme::Reset() noexcept
    {
        m_Opacity = 0.96f;
        m_Scale = 1.0f;
        m_ShowStatusBar = true;
        m_Accent[0] = 0.22f;
        m_Accent[1] = 0.55f;
        m_Accent[2] = 0.92f;
        m_Accent[3] = 1.0f;
    }

    float& MenuTheme::Opacity() noexcept
    {
        return m_Opacity;
    }

    float& MenuTheme::Scale() noexcept
    {
        return m_Scale;
    }

    bool& MenuTheme::ShowStatusBar() noexcept
    {
        return m_ShowStatusBar;
    }

    float* MenuTheme::AccentColor() noexcept
    {
        return m_Accent;
    }
}
