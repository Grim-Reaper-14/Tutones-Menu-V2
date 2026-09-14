#pragma once

#include "MenuPage.hpp"

namespace TutonesV2::UI
{
    class PageRenderer final
    {
    public:
        static void Render(MenuPage page) noexcept;
    };
}
