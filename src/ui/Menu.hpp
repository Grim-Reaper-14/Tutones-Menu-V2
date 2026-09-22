#pragma once

#include "MenuPage.hpp"

#include <atomic>

namespace TutonesV2::UI
{
    class Menu final
    {
    public:
        static Menu& Get() noexcept;

        void Toggle() noexcept;
        void SetOpen(bool open) noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        void SetPage(MenuPage page) noexcept;
        [[nodiscard]] MenuPage CurrentPage() const noexcept;
        void Render() noexcept;

    private:
        std::atomic_bool m_Open{};
        MenuPage m_Page{MenuPage::Self};
    };
}
