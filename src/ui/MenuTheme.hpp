#pragma once

namespace TutonesV2::UI
{
    class MenuTheme final
    {
    public:
        static MenuTheme& Get() noexcept;

        void Apply() noexcept;
        void Reset() noexcept;

        [[nodiscard]] float& Opacity() noexcept;
        [[nodiscard]] float& Scale() noexcept;
        [[nodiscard]] bool& ShowStatusBar() noexcept;
        [[nodiscard]] float* AccentColor() noexcept;

    private:
        float m_Opacity{0.96f};
        float m_Scale{1.0f};
        bool m_ShowStatusBar{true};
        float m_Accent[4]{0.22f, 0.55f, 0.92f, 1.0f};
    };
}
