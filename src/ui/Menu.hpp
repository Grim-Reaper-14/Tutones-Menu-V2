#pragma once

namespace TutonesV2::UI
{
    class Menu final
    {
    public:
        static Menu& Get() noexcept;
        void Render() noexcept;
    };
}
