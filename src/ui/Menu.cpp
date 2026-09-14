#include "Menu.hpp"

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
    }
}
