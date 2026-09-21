#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace TutonesV2::Features::Vehicle
{
    struct VehicleSnapshot final
    {
        bool busy{};
        bool lastSucceeded{};
        int lastSpawnedVehicle{};
        std::string message{"Ready"};
    };

    class VehicleService final
    {
    public:
        static VehicleService& Get() noexcept;
        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] VehicleSnapshot Snapshot() const;

        bool QueueSpawn(std::string modelName, bool enterVehicle, bool networked) noexcept;
        bool QueueRepairCurrent() noexcept;
        bool QueueCleanCurrent() noexcept;

    private:
        VehicleService() = default;
        bool EnsureLoop() noexcept;
        void SpawnTick() noexcept;
        void Finish(bool success, int vehicle, std::string message) noexcept;
        static std::uint32_t Joaat(const std::string& value) noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_Busy{};
        std::atomic_bool m_LoopQueued{};
        std::atomic_uint32_t m_PendingModel{};
        bool m_EnterVehicle{};
        bool m_Networked{true};
        int m_Attempts{};
        mutable std::mutex m_Mutex;
        VehicleSnapshot m_Snapshot{};
    };
}
