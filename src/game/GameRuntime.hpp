#pragma once

#include <atomic>

namespace TutonesV2::Game
{
    class GameRuntime final
    {
    public:
        static GameRuntime& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        std::atomic_bool m_Initialized{};
    };
}
