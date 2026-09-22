#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>

namespace TutonesV2::Features::World
{
    inline constexpr std::array<const char*, 17> WeatherCodes{{
        "EXTRASUNNY", "CLEAR", "CLOUDS", "SMOG", "FOGGY",
        "OVERCAST", "RAIN", "THUNDER", "CLEARING", "NEUTRAL",
        "SNOW", "BLIZZARD", "SNOWLIGHT", "XMAS", "HALLOWEEN",
        "RAIN_HALLOWEEN", "SNOW_HALLOWEEN"
    }};

    struct WorldSnapshot final
    {
        bool ready{};
        bool actionPending{};
        bool densityLoopRunning{};
        bool worldLoopRunning{};
        bool freezeClock{};
        bool weatherOverride{};
        bool blackout{};
        float pedDensity{1.0f};
        float scenarioPedDensity{1.0f};
        float vehicleDensity{1.0f};
        float randomVehicleDensity{1.0f};
        float parkedVehicleDensity{1.0f};
        int selectedHour{12};
        int selectedMinute{};
        int clockHour{-1};
        int clockMinute{-1};
        int weatherIndex{1};
        bool lastSucceeded{};
        std::string message{"Ready"};
    };

    class WorldService final
    {
    public:
        static WorldService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] WorldSnapshot Snapshot() const;

        void SetPedDensity(float value) noexcept;
        void SetScenarioPedDensity(float value) noexcept;
        void SetVehicleDensity(float value) noexcept;
        void SetRandomVehicleDensity(float value) noexcept;
        void SetParkedVehicleDensity(float value) noexcept;
        void ResetDensity() noexcept;

        bool QueueSetTime(int hour, int minute) noexcept;
        bool SetFreezeClock(bool enabled) noexcept;
        bool QueueWeather(int weatherIndex) noexcept;
        bool QueueClearWeather() noexcept;
        bool QueueBlackout(bool enabled) noexcept;

        bool QueueClearPeds(float radius) noexcept;
        bool QueueClearVehicles(float radius) noexcept;
        bool QueueClearObjects(float radius) noexcept;
        bool QueueClearAmbient(float radius) noexcept;

        void RequestClockSample() noexcept;

    private:
        WorldService() = default;

        static float ClampDensity(float value) noexcept;
        [[nodiscard]] bool HasDensityOverride() const noexcept;
        [[nodiscard]] bool HasWorldOverride() const noexcept;

        void EnsureDensityLoop() noexcept;
        void DensityTick() noexcept;
        void EnsureWorldLoop() noexcept;
        void WorldTick() noexcept;

        template<typename Fn>
        bool QueueAction(std::string message, Fn&& fn)
        {
            if (!IsReady())
                return false;

            bool expected = false;
            if (!m_ActionPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return false;

            {
                std::scoped_lock lock(m_Mutex);
                m_Snapshot.message = std::move(message);
                m_Snapshot.lastSucceeded = false;
            }

            if (GameRuntimeEnqueue([this, action = std::forward<Fn>(fn)]() mutable {
                    const bool success = action();
                    FinishAction(success, success ? "World action complete" : "World action failed");
                }))
            {
                return true;
            }

            m_ActionPending.store(false, std::memory_order_release);
            return false;
        }

        bool GameRuntimeEnqueue(std::function<void()> task);
        void FinishAction(bool success, std::string message) noexcept;
        [[nodiscard]] int PlayerPed() const noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_ActionPending{};
        std::atomic_bool m_DensityLoopQueued{};
        std::atomic_bool m_WorldLoopQueued{};
        std::atomic_bool m_ClockSamplePending{};

        std::atomic<float> m_PedDensity{1.0f};
        std::atomic<float> m_ScenarioPedDensity{1.0f};
        std::atomic<float> m_VehicleDensity{1.0f};
        std::atomic<float> m_RandomVehicleDensity{1.0f};
        std::atomic<float> m_ParkedVehicleDensity{1.0f};

        std::atomic_bool m_FreezeClock{};
        std::atomic_bool m_WeatherOverride{};
        std::atomic_bool m_Blackout{};
        std::atomic_int m_SelectedHour{12};
        std::atomic_int m_SelectedMinute{};
        std::atomic_int m_ClockHour{-1};
        std::atomic_int m_ClockMinute{-1};
        std::atomic_int m_WeatherIndex{1};

        mutable std::mutex m_Mutex;
        WorldSnapshot m_Snapshot{};
    };
}
