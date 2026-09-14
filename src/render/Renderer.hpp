#pragma once

#include <atomic>

namespace TutonesV2::Render
{
    class Renderer final
    {
    public:
        static Renderer& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        std::atomic_bool m_Initialized{};
    };
}
