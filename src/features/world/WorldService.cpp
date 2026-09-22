#include "WorldService.hpp"

#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace TutonesV2::Features::World
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;

        constexpr float DensityEpsilon = 0.001f;

        bool ApplyClearArea(int ped, float radius, int kind) noexcept
        {
            if (ped == 0)
                return false;

            const auto coords = NativeInvoker::Invoke<Game::Native::NativeVector3>(
                NativeId::GetEntityCoords,
                ped,
                std::int32_t{0});
            if (!coords)
                return false;

            switch (kind)
            {
            case 0:
                return NativeInvoker::InvokeVoid(
                    NativeId::ClearAreaOfPeds,
                    coords->x, coords->y, coords->z, radius, 0);
            case 1:
                return NativeInvoker::InvokeVoid(
                    NativeId::ClearAreaOfVehicles,
                    coords->x, coords->y, coords->z, radius,
                    0, 0, 0, 0, 0, 0, 0);
            case 2:
                return NativeInvoker::InvokeVoid(
                    NativeId::ClearAreaOfObjects,
                    coords->x, coords->y, coords->z, radius, 0);
            default:
                return false;
            }
        }
    }

    WorldService& WorldService::Get() noexcept
    {
        static WorldService instance;
        return instance;
    }

    bool WorldService::Initialize() noexcept
    {
        m_ActionPending.store(false, std::memory_order_release);
        m_DensityLoopQueued.store(false, std::memory_order_release);
        m_WorldLoopQueued.store(false, std::memory_order_release);
        m_ClockSamplePending.store(false, std::memory_order_release);
        m_PedDensity.store(1.0f, std::memory_order_release);
        m_ScenarioPedDensity.store(1.0f, std::memory_order_release);
        m_VehicleDensity.store(1.0f, std::memory_order_release);
        m_RandomVehicleDensity.store(1.0f, std::memory_order_release);
        m_ParkedVehicleDensity.store(1.0f, std::memory_order_release);
        m_FreezeClock.store(false, std::memory_order_release);
        m_WeatherOverride.store(false, std::memory_order_release);
        m_Blackout.store(false, std::memory_order_release);
        m_SelectedHour.store(12, std::memory_order_release);
        m_SelectedMinute.store(0, std::memory_order_release);
        m_ClockHour.store(-1, std::memory_order_release);
        m_ClockMinute.store(-1, std::memory_order_release);
        m_WeatherIndex.store(1, std::memory_order_release);

        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot = {};
            m_Snapshot.ready = true;
        }

        m_Ready.store(true, std::memory_order_release);
        return true;
    }

    void WorldService::Shutdown() noexcept
    {
        if (!m_Ready.exchange(false, std::memory_order_acq_rel))
            return;

        m_DensityLoopQueued.store(false, std::memory_order_release);
        m_WorldLoopQueued.store(false, std::memory_order_release);
        m_ActionPending.store(false, std::memory_order_release);
        m_FreezeClock.store(false, std::memory_order_release);
        m_WeatherOverride.store(false, std::memory_order_release);
        m_Blackout.store(false, std::memory_order_release);

        if (Game::GameRuntime::Get().NativeReady())
        {
            static_cast<void>(Game::GameRuntime::Get().Enqueue([] {
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::NetworkClearClockTimeOverride));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearOverrideWeather));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetArtificialLightsState, std::int32_t{0}));
            }));
        }
    }

    bool WorldService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire)
            && Game::GameRuntime::Get().NativeReady();
    }

    WorldSnapshot WorldService::Snapshot() const
    {
        WorldSnapshot snapshot{};
        snapshot.ready = IsReady();
        snapshot.actionPending = m_ActionPending.load(std::memory_order_acquire);
        snapshot.densityLoopRunning = m_DensityLoopQueued.load(std::memory_order_acquire);
        snapshot.worldLoopRunning = m_WorldLoopQueued.load(std::memory_order_acquire);
        snapshot.freezeClock = m_FreezeClock.load(std::memory_order_acquire);
        snapshot.weatherOverride = m_WeatherOverride.load(std::memory_order_acquire);
        snapshot.blackout = m_Blackout.load(std::memory_order_acquire);
        snapshot.pedDensity = m_PedDensity.load(std::memory_order_acquire);
        snapshot.scenarioPedDensity = m_ScenarioPedDensity.load(std::memory_order_acquire);
        snapshot.vehicleDensity = m_VehicleDensity.load(std::memory_order_acquire);
        snapshot.randomVehicleDensity = m_RandomVehicleDensity.load(std::memory_order_acquire);
        snapshot.parkedVehicleDensity = m_ParkedVehicleDensity.load(std::memory_order_acquire);
        snapshot.selectedHour = m_SelectedHour.load(std::memory_order_acquire);
        snapshot.selectedMinute = m_SelectedMinute.load(std::memory_order_acquire);
        snapshot.clockHour = m_ClockHour.load(std::memory_order_acquire);
        snapshot.clockMinute = m_ClockMinute.load(std::memory_order_acquire);
        snapshot.weatherIndex = m_WeatherIndex.load(std::memory_order_acquire);

        std::scoped_lock lock(m_Mutex);
        snapshot.lastSucceeded = m_Snapshot.lastSucceeded;
        snapshot.message = m_Snapshot.message;
        return snapshot;
    }

    float WorldService::ClampDensity(float value) noexcept
    {
        if (!std::isfinite(value))
            return 1.0f;
        return std::clamp(value, 0.0f, 1.0f);
    }

    void WorldService::SetPedDensity(float value) noexcept
    {
        m_PedDensity.store(ClampDensity(value), std::memory_order_release);
        EnsureDensityLoop();
    }

    void WorldService::SetScenarioPedDensity(float value) noexcept
    {
        m_ScenarioPedDensity.store(ClampDensity(value), std::memory_order_release);
        EnsureDensityLoop();
    }

    void WorldService::SetVehicleDensity(float value) noexcept
    {
        m_VehicleDensity.store(ClampDensity(value), std::memory_order_release);
        EnsureDensityLoop();
    }

    void WorldService::SetRandomVehicleDensity(float value) noexcept
    {
        m_RandomVehicleDensity.store(ClampDensity(value), std::memory_order_release);
        EnsureDensityLoop();
    }

    void WorldService::SetParkedVehicleDensity(float value) noexcept
    {
        m_ParkedVehicleDensity.store(ClampDensity(value), std::memory_order_release);
        EnsureDensityLoop();
    }

    void WorldService::ResetDensity() noexcept
    {
        m_PedDensity.store(1.0f, std::memory_order_release);
        m_ScenarioPedDensity.store(1.0f, std::memory_order_release);
        m_VehicleDensity.store(1.0f, std::memory_order_release);
        m_RandomVehicleDensity.store(1.0f, std::memory_order_release);
        m_ParkedVehicleDensity.store(1.0f, std::memory_order_release);
    }

    bool WorldService::HasDensityOverride() const noexcept
    {
        return std::fabs(m_PedDensity.load(std::memory_order_acquire) - 1.0f) > DensityEpsilon
            || std::fabs(m_ScenarioPedDensity.load(std::memory_order_acquire) - 1.0f) > DensityEpsilon
            || std::fabs(m_VehicleDensity.load(std::memory_order_acquire) - 1.0f) > DensityEpsilon
            || std::fabs(m_RandomVehicleDensity.load(std::memory_order_acquire) - 1.0f) > DensityEpsilon
            || std::fabs(m_ParkedVehicleDensity.load(std::memory_order_acquire) - 1.0f) > DensityEpsilon;
    }

    bool WorldService::HasWorldOverride() const noexcept
    {
        return m_FreezeClock.load(std::memory_order_acquire)
            || m_WeatherOverride.load(std::memory_order_acquire)
            || m_Blackout.load(std::memory_order_acquire);
    }

    void WorldService::EnsureDensityLoop() noexcept
    {
        if (!IsReady() || !HasDensityOverride())
            return;

        bool expected = false;
        if (!m_DensityLoopQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        if (!GameRuntimeEnqueue([this] { DensityTick(); }))
            m_DensityLoopQueued.store(false, std::memory_order_release);
    }

    void WorldService::DensityTick() noexcept
    {
        if (!IsReady() || !HasDensityOverride())
        {
            m_DensityLoopQueued.store(false, std::memory_order_release);
            return;
        }

        const float ped = m_PedDensity.load(std::memory_order_acquire);
        const float scenario = m_ScenarioPedDensity.load(std::memory_order_acquire);
        const float vehicle = m_VehicleDensity.load(std::memory_order_acquire);
        const float randomVehicle = m_RandomVehicleDensity.load(std::memory_order_acquire);
        const float parked = m_ParkedVehicleDensity.load(std::memory_order_acquire);

        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedDensityMultiplierThisFrame, ped));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetScenarioPedDensityMultiplierThisFrame, scenario, scenario));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetVehicleDensityMultiplierThisFrame, vehicle));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetRandomVehicleDensityMultiplierThisFrame, randomVehicle));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetParkedVehicleDensityMultiplierThisFrame, parked));

        if (!GameRuntimeEnqueue([this] { DensityTick(); }))
            m_DensityLoopQueued.store(false, std::memory_order_release);
    }

    void WorldService::EnsureWorldLoop() noexcept
    {
        if (!IsReady() || !HasWorldOverride())
            return;

        bool expected = false;
        if (!m_WorldLoopQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        if (!GameRuntimeEnqueue([this] { WorldTick(); }))
            m_WorldLoopQueued.store(false, std::memory_order_release);
    }

    void WorldService::WorldTick() noexcept
    {
        if (!IsReady() || !HasWorldOverride())
        {
            m_WorldLoopQueued.store(false, std::memory_order_release);
            return;
        }

        if (m_FreezeClock.load(std::memory_order_acquire))
        {
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::NetworkOverrideClockTime,
                m_SelectedHour.load(std::memory_order_acquire),
                m_SelectedMinute.load(std::memory_order_acquire),
                0));
        }

        if (m_WeatherOverride.load(std::memory_order_acquire))
        {
            const int index = std::clamp(
                m_WeatherIndex.load(std::memory_order_acquire),
                0,
                static_cast<int>(WeatherCodes.size()) - 1);
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetOverrideWeather,
                WeatherCodes[static_cast<std::size_t>(index)]));
        }

        if (m_Blackout.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetArtificialLightsState, std::int32_t{1}));

        if (!GameRuntimeEnqueue([this] { WorldTick(); }))
            m_WorldLoopQueued.store(false, std::memory_order_release);
    }

    bool WorldService::QueueSetTime(int hour, int minute) noexcept
    {
        hour = std::clamp(hour, 0, 23);
        minute = std::clamp(minute, 0, 59);
        m_SelectedHour.store(hour, std::memory_order_release);
        m_SelectedMinute.store(minute, std::memory_order_release);

        return QueueAction("Set GTA Online clock", [this, hour, minute] {
            const bool ok = NativeInvoker::InvokeVoid(
                NativeId::NetworkOverrideClockTime,
                hour,
                minute,
                0);
            if (ok && m_FreezeClock.load(std::memory_order_acquire))
                EnsureWorldLoop();
            return ok;
        });
    }

    bool WorldService::SetFreezeClock(bool enabled) noexcept
    {
        if (!IsReady())
            return false;

        m_FreezeClock.store(enabled, std::memory_order_release);
        if (enabled)
        {
            EnsureWorldLoop();
            return true;
        }

        return GameRuntimeEnqueue([] {
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::NetworkClearClockTimeOverride));
        });
    }

    bool WorldService::QueueWeather(int weatherIndex) noexcept
    {
        weatherIndex = std::clamp(weatherIndex, 0, static_cast<int>(WeatherCodes.size()) - 1);
        m_WeatherIndex.store(weatherIndex, std::memory_order_release);
        const std::string weather = WeatherCodes[static_cast<std::size_t>(weatherIndex)];

        return QueueAction("Apply weather override", [this, weather] {
            bool ok = NativeInvoker::InvokeVoid(NativeId::SetWeatherTypePersist, weather.c_str());
            ok = NativeInvoker::InvokeVoid(NativeId::SetWeatherTypeNowPersist, weather.c_str()) && ok;
            ok = NativeInvoker::InvokeVoid(NativeId::SetOverrideWeather, weather.c_str()) && ok;
            if (ok)
            {
                m_WeatherOverride.store(true, std::memory_order_release);
                EnsureWorldLoop();
            }
            return ok;
        });
    }

    bool WorldService::QueueClearWeather() noexcept
    {
        return QueueAction("Clear weather override", [this] {
            const bool ok = NativeInvoker::InvokeVoid(NativeId::ClearOverrideWeather);
            if (ok)
                m_WeatherOverride.store(false, std::memory_order_release);
            return ok;
        });
    }

    bool WorldService::QueueBlackout(bool enabled) noexcept
    {
        return QueueAction(enabled ? "Enable blackout" : "Disable blackout", [this, enabled] {
            const bool ok = NativeInvoker::InvokeVoid(
                NativeId::SetArtificialLightsState,
                std::int32_t{enabled ? 1 : 0});
            if (ok)
            {
                m_Blackout.store(enabled, std::memory_order_release);
                if (enabled)
                    EnsureWorldLoop();
            }
            return ok;
        });
    }

    int WorldService::PlayerPed() const noexcept
    {
        const auto ped = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
        return ped ? *ped : 0;
    }

    bool WorldService::QueueClearPeds(float radius) noexcept
    {
        radius = std::clamp(radius, 5.0f, 250.0f);
        return QueueAction("Clear nearby peds", [this, radius] {
            return ApplyClearArea(PlayerPed(), radius, 0);
        });
    }

    bool WorldService::QueueClearVehicles(float radius) noexcept
    {
        radius = std::clamp(radius, 5.0f, 250.0f);
        return QueueAction("Clear nearby vehicles", [this, radius] {
            return ApplyClearArea(PlayerPed(), radius, 1);
        });
    }

    bool WorldService::QueueClearObjects(float radius) noexcept
    {
        radius = std::clamp(radius, 5.0f, 250.0f);
        return QueueAction("Clear nearby objects", [this, radius] {
            return ApplyClearArea(PlayerPed(), radius, 2);
        });
    }

    bool WorldService::QueueClearAmbient(float radius) noexcept
    {
        radius = std::clamp(radius, 5.0f, 250.0f);
        return QueueAction("Clear nearby ambient world", [this, radius] {
            const int ped = PlayerPed();
            if (ped == 0)
                return false;
            bool ok = ApplyClearArea(ped, radius, 0);
            ok = ApplyClearArea(ped, radius, 1) && ok;
            ok = ApplyClearArea(ped, radius, 2) && ok;
            return ok;
        });
    }

    void WorldService::RestoreSavedState(
        bool freezeClock,
        bool blackout,
        bool weatherOverride,
        int weatherIndex,
        float pedDensity,
        float scenarioPedDensity,
        float vehicleDensity,
        float randomVehicleDensity,
        float parkedVehicleDensity,
        int hour,
        int minute) noexcept
    {
        if (!IsReady())
            return;

        m_SelectedHour.store(std::clamp(hour, 0, 23), std::memory_order_release);
        m_SelectedMinute.store(std::clamp(minute, 0, 59), std::memory_order_release);
        m_WeatherIndex.store(
            std::clamp(weatherIndex, 0, static_cast<int>(WeatherCodes.size()) - 1),
            std::memory_order_release);
        m_FreezeClock.store(freezeClock, std::memory_order_release);
        m_Blackout.store(blackout, std::memory_order_release);
        m_WeatherOverride.store(weatherOverride, std::memory_order_release);
        m_PedDensity.store(ClampDensity(pedDensity), std::memory_order_release);
        m_ScenarioPedDensity.store(ClampDensity(scenarioPedDensity), std::memory_order_release);
        m_VehicleDensity.store(ClampDensity(vehicleDensity), std::memory_order_release);
        m_RandomVehicleDensity.store(ClampDensity(randomVehicleDensity), std::memory_order_release);
        m_ParkedVehicleDensity.store(ClampDensity(parkedVehicleDensity), std::memory_order_release);

        static_cast<void>(GameRuntimeEnqueue([this] {
            if (m_FreezeClock.load(std::memory_order_acquire))
            {
                static_cast<void>(NativeInvoker::InvokeVoid(
                    NativeId::NetworkOverrideClockTime,
                    m_SelectedHour.load(std::memory_order_acquire),
                    m_SelectedMinute.load(std::memory_order_acquire),
                    0));
            }
            else
            {
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::NetworkClearClockTimeOverride));
            }

            if (m_WeatherOverride.load(std::memory_order_acquire))
            {
                const int index = std::clamp(
                    m_WeatherIndex.load(std::memory_order_acquire),
                    0,
                    static_cast<int>(WeatherCodes.size()) - 1);
                const char* weather = WeatherCodes[static_cast<std::size_t>(index)];
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetWeatherTypePersist, weather));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetWeatherTypeNowPersist, weather));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetOverrideWeather, weather));
            }
            else
            {
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearOverrideWeather));
            }

            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetArtificialLightsState,
                std::int32_t{m_Blackout.load(std::memory_order_acquire) ? 1 : 0}));
        }));

        EnsureDensityLoop();
        EnsureWorldLoop();
    }

    void WorldService::RequestClockSample() noexcept
    {
        if (!IsReady())
            return;

        bool expected = false;
        if (!m_ClockSamplePending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        if (!GameRuntimeEnqueue([this] {
                const auto hour = NativeInvoker::Invoke<std::int32_t>(NativeId::GetClockHours);
                const auto minute = NativeInvoker::Invoke<std::int32_t>(NativeId::GetClockMinutes);
                if (hour)
                    m_ClockHour.store(*hour, std::memory_order_release);
                if (minute)
                    m_ClockMinute.store(*minute, std::memory_order_release);
                m_ClockSamplePending.store(false, std::memory_order_release);
            }))
        {
            m_ClockSamplePending.store(false, std::memory_order_release);
        }
    }

    bool WorldService::GameRuntimeEnqueue(std::function<void()> task)
    {
        return Game::GameRuntime::Get().Enqueue(std::move(task));
    }

    void WorldService::FinishAction(bool success, std::string message) noexcept
    {
        m_ActionPending.store(false, std::memory_order_release);
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.lastSucceeded = success;
        m_Snapshot.message = std::move(message);
    }
}
