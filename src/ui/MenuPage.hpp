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
        Online,
        World,
        Business,
        Recovery,
        Protections,
        Settings,
        Misc,
        Tools,
        Heists,
    };

    struct MenuPageDescriptor final
    {
        MenuPage Page;
        const char* Label;
    };

    inline constexpr std::array<MenuPageDescriptor, 12> MenuPages{{
        {MenuPage::Self, "Self"},
        {MenuPage::Weapons, "Weapons"},
        {MenuPage::Vehicle, "Vehicles"},
        {MenuPage::Online, "Online"},
        {MenuPage::World, "World"},
        {MenuPage::Business, "Businesses"},
        {MenuPage::Recovery, "Recovery"},
        {MenuPage::Protections, "Protections"},
        {MenuPage::Settings, "Settings"},
        {MenuPage::Misc, "Utilities"},
        {MenuPage::Tools, "Tools"},
        {MenuPage::Heists, "Heists"},
    }};

    [[nodiscard]] inline const char* MenuPageName(MenuPage page) noexcept
    {
        switch (page)
        {
        case MenuPage::Self: return "Self";
        case MenuPage::Weapons: return "Weapons";
        case MenuPage::Vehicle: return "Vehicles";
        case MenuPage::Online: return "Online";
        case MenuPage::World: return "World";
        case MenuPage::Business: return "Businesses";
        case MenuPage::Recovery: return "Recovery";
        case MenuPage::Protections: return "Protections";
        case MenuPage::Settings: return "Settings";
        case MenuPage::Misc: return "Utilities";
        case MenuPage::Tools: return "Tools";
        case MenuPage::Heists: return "Heists";
        }

        return "Self";
    }
}
