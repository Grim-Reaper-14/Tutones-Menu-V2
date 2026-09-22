#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace TutonesV2::Features::Vehicle
{
    struct VehicleSnapshot final
    {
        bool busy{};
        bool lastSucceeded{};
        int lastSpawnedVehicle{};
        std::string message{"Ready"};
    };

    struct VehicleCatalogSnapshot final
    {
        std::vector<int> classes{};
        std::vector<std::string> displayNames{};
        std::size_t ready{};
        std::size_t total{};
        bool loading{};
    };

    class VehicleService final
    {
    public:
        static VehicleService& Get() noexcept;
        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] VehicleSnapshot Snapshot() const;
        [[nodiscard]] VehicleCatalogSnapshot CatalogSnapshot() const;
        void EnsureCatalog() noexcept;

        bool QueueSpawn(std::string modelName, bool enterVehicle, bool networked, bool maxed = false) noexcept;
        bool QueueRepairCurrent() noexcept;
        bool QueueCleanCurrent() noexcept;
        bool QueueSetUpright() noexcept;

        bool SetVehicleGodMode(bool enabled) noexcept;
        bool SetKeepVehicleClean(bool enabled) noexcept;
        bool SetHornBoost(bool enabled) noexcept;

        [[nodiscard]] bool VehicleGodMode() const noexcept;
        [[nodiscard]] bool KeepVehicleClean() const noexcept;
        [[nodiscard]] bool HornBoost() const noexcept;

    private:
        VehicleService() = default;
        bool EnsureLoop() noexcept;
        void SpawnTick() noexcept;
        void CatalogTick() noexcept;
        bool MaxVehicle(int vehicle) noexcept;
        void EnsureFeatureLoop() noexcept;
        void FeatureTick() noexcept;
        [[nodiscard]] bool HasFeatureLoopWork() const noexcept;
        void RestoreGodVehicle() noexcept;
        void ResetHornBoost() noexcept;
        void Finish(bool success, int vehicle, std::string message) noexcept;
        static std::uint32_t Joaat(const std::string& value) noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_Busy{};
        std::atomic_bool m_LoopQueued{};
        std::atomic_bool m_FeatureLoopQueued{};
        std::atomic_bool m_CatalogLoopQueued{};
        std::atomic_bool m_VehicleGodMode{};
        std::atomic_bool m_KeepVehicleClean{};
        std::atomic_bool m_HornBoost{};
        std::atomic_uint32_t m_PendingModel{};
        bool m_EnterVehicle{};
        bool m_Networked{true};
        bool m_Maxed{};
        int m_Attempts{};
        int m_LastGodVehicle{};
        float m_BoostSpeed{10.0f};
        bool m_WasHornPressed{};
        std::size_t m_CatalogCursor{};
        mutable std::mutex m_Mutex;
        VehicleSnapshot m_Snapshot{};
        std::vector<int> m_CatalogClasses{};
        std::vector<std::string> m_CatalogDisplayNames{};
    };
}
