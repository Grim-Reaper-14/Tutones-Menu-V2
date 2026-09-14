#pragma once

#include <atomic>

namespace TutonesV2::Hooking
{
    class HookManager final
    {
    public:
        static HookManager& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        std::atomic_bool m_Initialized{};
    };
}
