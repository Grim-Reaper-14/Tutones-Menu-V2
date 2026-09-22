#pragma once

#include <array>
#include <cstdint>

namespace TutonesV2::UI
{
    enum class MenuPage : std::uint8_t
    {
        Self,
        Weapons,
        Vehicle,
        Teleport,
        World,
        Recovery,
        Settings,
        Online,
        Misc,
    };

    struct MenuPageDescriptor final
    {
        MenuPage Page;
        const char* Label;
    };

    inline constexpr std::array<MenuPageDescriptor, 9> MenuPages{{
        {MenuPage::Self, "Self"},
        {MenuPage::Weapons, "Weapons"},
        {MenuPage::Vehicle, "Vehicle"},
        {MenuPage::Online, "Online"},
        {MenuPage::Teleport, "Teleport"},
        {MenuPage::World, "World"},
        {MenuPage::Recovery, "Recovery"},
        {MenuPage::Misc, "Misc"},
        {MenuPage::Settings, "Settings"},
    }};

    [[nodiscard]] inline const char* MenuPageName(MenuPage page) noexcept
    {
        switch (page)
        {
        case MenuPage::Self: return "Self";
        case MenuPage::Weapons: return "Weapons";
        case MenuPage::Vehicle: return "Vehicle";
        case MenuPage::Teleport: return "Teleport";
        case MenuPage::World: return "World";
        case MenuPage::Recovery: return "Recovery";
        case MenuPage::Settings: return "Settings";
        case MenuPage::Online: return "Online";
        case MenuPage::Misc: return "Misc";
        }

        return "Self";
    }
}
