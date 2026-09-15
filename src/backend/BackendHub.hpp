#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace TutonesV2::Backend
{
    enum class BackendCommandKind : std::uint8_t
    {
        SelfRestore,
        VehicleRepair,
        TeleportWaypoint,
        TeleportObjective,
    };

    struct BackendCommand final
    {
        BackendCommandKind Kind{};
    };

    class BackendHub final
    {
    public:
        static BackendHub& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool Submit(BackendCommand command) noexcept;
        [[nodiscard]] bool TryPop(BackendCommand& command) noexcept;
        [[nodiscard]] std::size_t PendingCount() const noexcept;
        void ClearCommands() noexcept;

        static constexpr std::size_t CommandCapacity = 64;

    private:
        std::atomic_bool m_Initialized{};
        mutable std::mutex m_CommandMutex;
        std::array<BackendCommand, CommandCapacity> m_Commands{};
        std::size_t m_CommandHead{};
        std::size_t m_CommandCount{};
    };
}
