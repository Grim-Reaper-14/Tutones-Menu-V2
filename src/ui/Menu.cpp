#include "Menu.hpp"

namespace TutonesV2::UI
{
    Menu& Menu::Get() noexcept
    {
        static Menu instance;
        return instance;
    }

    void Menu::Render() noexcept
    {
        // Intentionally empty in the clean base.
        // UI work is added only after the DX12 lifecycle is verified stable.
    }
}
