#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace TutonesV2::Features::Vehicle
{
    struct VehicleConvenienceSnapshot final
    {
        bool ready{};
        bool keepFixed{};
        bool seatbelt{};
        bool keepEngineRunning{};
        bool keepHeadlightsOn{};
        bool highBeams{};
        bool allowHats{};
        bool speedReadout{};
        bool loweredStance{};
        float speedMps{};
        float speedKph{};
        float speedMph{};
        bool actionPending{};
        bool haveResult{};
        bool lastSucceeded{};
        int currentVehicle{};
        std::string message{"Ready"};
    };

    class VehicleConvenienceService final
    {
    public:
        static VehicleConvenienceService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] VehicleConvenienceSnapshot Snapshot() const;

        void SetKeepFixed(bool enabled) noexcept;
        void SetSeatbelt(bool enabled) noexcept;
        void SetKeepEngineRunning(bool enabled) noexcept;
        void SetKeepHeadlightsOn(bool enabled) noexcept;
        void SetHighBeams(bool enabled) noexcept;
        void SetAllowHats(bool enabled) noexcept;
        void SetSpeedReadout(bool enabled) noexcept;
        void SetLoweredStance(bool enabled) noexcept;

        bool QueueSetEngine(bool enabled) noexcept;
        bool QueueSetDoorsLocked(bool locked) noexcept;
        bool QueueEnterLastVehicle() noexcept;

    private:
        VehicleConvenienceService() = default;

        [[nodiscard]] bool HasPersistentWork() const noexcept;
        void EnsureLoop() noexcept;
        void Tick() noexcept;

        [[nodiscard]] int CurrentPed() const noexcept;
        [[nodiscard]] int CurrentVehicle() const noexcept;
        void ApplyKeepFixed(int vehicle) noexcept;
        void ApplySeatbelt(int ped, bool enabled) noexcept;
        void RestoreSeatbelt() noexcept;
        void RestoreEngineState() noexcept;
        void RestoreLights() noexcept;
        void RestoreStance() noexcept;

        bool QueueAction(std::string pendingMessage, std::function<void()> action) noexcept;
        void FinishAction(bool success, std::string message) noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_LoopQueued{};
        std::atomic_bool m_ActionPending{};
        std::atomic_bool m_KeepFixed{};
        std::atomic_bool m_Seatbelt{};
        std::atomic_bool m_KeepEngineRunning{};
        std::atomic_bool m_KeepHeadlightsOn{};
        std::atomic_bool m_HighBeams{};
        std::atomic_bool m_AllowHats{};
        std::atomic_bool m_SpeedReadout{};
        std::atomic_bool m_LoweredStance{};
        std::atomic<float> m_SpeedMps{};

        int m_LastSeatbeltPed{};
        int m_LastEnginePed{};
        int m_LastLightsVehicle{};
        int m_LastStanceVehicle{};

        mutable std::mutex m_Mutex;
        bool m_HaveResult{};
        bool m_LastSucceeded{};
        int m_CurrentVehicleView{};
        std::string m_Message{"Ready"};
    };
}
