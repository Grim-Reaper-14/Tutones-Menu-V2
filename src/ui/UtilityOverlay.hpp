#pragma once

namespace TutonesV2::UI
{
    class UtilityOverlay final
    {
    public:
        [[nodiscard]] static bool AnyVisible() noexcept;
        static void Render() noexcept;
    };
}
