#include "VehicleConvenienceService.hpp"

#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"

#include <cmath>
#include <utility>

namespace TutonesV2::Features::Vehicle
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;

        constexpr int WillFlyThroughWindscreenFlag = 32;
        constexpr int LeaveEngineOnWhenExitingVehiclesFlag = 241;
        constexpr int KeepHatInVehicleResetFlag = 337;
        constexpr int KnockOffNever = 1;
        constexpr int KnockOffDefault = 0;
        constexpr int LightsNormal = 0;
        constexpr int LightsForcedOn = 2;

        bool EntityExists(int entity) noexcept
        {
            if (entity == 0)
                return false;

            const auto exists = NativeInvoker::Invoke<std::int32_t>(
                NativeId::DoesEntityExist,
                entity);
            return exists && *exists != 0;
        }
    }

    VehicleConvenienceService& VehicleConvenienceService::Get() noexcept
    {
        static VehicleConvenienceService instance;
        return instance;
    }

    bool VehicleConvenienceService::Initialize() noexcept
    {
        m_LoopQueued.store(false, std::memory_order_release);
        m_ActionPending.store(false, std::memory_order_release);
        m_KeepFixed.store(false, std::memory_order_release);
        m_Seatbelt.store(false, std::memory_order_release);
        m_KeepEngineRunning.store(false, std::memory_order_release);
        m_KeepHeadlightsOn.store(false, std::memory_order_release);
        m_HighBeams.store(false, std::memory_order_release);
        m_AllowHats.store(false, std::memory_order_release);
        m_SpeedReadout.store(false, std::memory_order_release);
        m_LoweredStance.store(false, std::memory_order_release);
        m_SpeedMps.store(0.0f, std::memory_order_release);
        m_LastSeatbeltPed = 0;
        m_LastEnginePed = 0;
        m_LastLightsVehicle = 0;
        m_LastStanceVehicle = 0;

        {
            std::scoped_lock lock(m_Mutex);
            m_HaveResult = false;
            m_LastSucceeded = false;
            m_CurrentVehicleView = 0;
            m_Message = "Ready";
        }

        m_Ready.store(true, std::memory_order_release);
        return true;
    }

    void VehicleConvenienceService::Shutdown() noexcept
    {
        if (!m_Ready.exchange(false, std::memory_order_acq_rel))
            return;

        m_KeepFixed.store(false, std::memory_order_release);
        m_Seatbelt.store(false, std::memory_order_release);
        m_KeepEngineRunning.store(false, std::memory_order_release);
        m_KeepHeadlightsOn.store(false, std::memory_order_release);
        m_HighBeams.store(false, std::memory_order_release);
        m_AllowHats.store(false, std::memory_order_release);
        m_SpeedReadout.store(false, std::memory_order_release);
        m_LoweredStance.store(false, std::memory_order_release);
        m_SpeedMps.store(0.0f, std::memory_order_release);

        if (Game::GameRuntime::Get().NativeReady())
        {
            static_cast<void>(Game::GameRuntime::Get().Enqueue([this] {
                RestoreSeatbelt();
                RestoreEngineState();
                RestoreLights();
                RestoreStance();
            }));
        }

        m_LoopQueued.store(false, std::memory_order_release);
    }

    bool VehicleConvenienceService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire)
            && Game::GameRuntime::Get().NativeReady();
    }

    VehicleConvenienceSnapshot VehicleConvenienceService::Snapshot() const
    {
        VehicleConvenienceSnapshot out{};
        out.ready = IsReady();
        out.keepFixed = m_KeepFixed.load(std::memory_order_acquire);
        out.seatbelt = m_Seatbelt.load(std::memory_order_acquire);
        out.keepEngineRunning = m_KeepEngineRunning.load(std::memory_order_acquire);
        out.keepHeadlightsOn = m_KeepHeadlightsOn.load(std::memory_order_acquire);
        out.highBeams = m_HighBeams.load(std::memory_order_acquire);
        out.allowHats = m_AllowHats.load(std::memory_order_acquire);
        out.speedReadout = m_SpeedReadout.load(std::memory_order_acquire);
        out.loweredStance = m_LoweredStance.load(std::memory_order_acquire);
        out.speedMps = m_SpeedMps.load(std::memory_order_acquire);
        out.speedKph = out.speedMps * 3.6f;
        out.speedMph = out.speedMps * 2.23693629f;
        out.actionPending = m_ActionPending.load(std::memory_order_acquire);

        std::scoped_lock lock(m_Mutex);
        out.haveResult = m_HaveResult;
        out.lastSucceeded = m_LastSucceeded;
        out.currentVehicle = m_CurrentVehicleView;
        out.message = m_Message;
        return out;
    }

    int VehicleConvenienceService::CurrentPed() const noexcept
    {
        const auto ped = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
        return ped ? *ped : 0;
    }

    int VehicleConvenienceService::CurrentVehicle() const noexcept
    {
        const int ped = CurrentPed();
        if (ped == 0)
            return 0;

        const auto inVehicle = NativeInvoker::Invoke<std::int32_t>(
            NativeId::IsPedInAnyVehicle,
            ped,
            std::int32_t{0});
        if (!inVehicle || *inVehicle == 0)
            return 0;

        const auto vehicle = NativeInvoker::Invoke<std::int32_t>(
            NativeId::GetVehiclePedIsUsing,
            ped);
        if (!vehicle || *vehicle == 0 || !EntityExists(*vehicle))
            return 0;
        return *vehicle;
    }

    bool VehicleConvenienceService::HasPersistentWork() const noexcept
    {
        return m_KeepFixed.load(std::memory_order_acquire)
            || m_Seatbelt.load(std::memory_order_acquire)
            || m_KeepEngineRunning.load(std::memory_order_acquire)
            || m_KeepHeadlightsOn.load(std::memory_order_acquire)
            || m_HighBeams.load(std::memory_order_acquire)
            || m_AllowHats.load(std::memory_order_acquire)
            || m_SpeedReadout.load(std::memory_order_acquire)
            || m_LoweredStance.load(std::memory_order_acquire);
    }

    void VehicleConvenienceService::EnsureLoop() noexcept
    {
        if (!IsReady() || !HasPersistentWork())
            return;

        bool expected = false;
        if (!m_LoopQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        if (!Game::GameRuntime::Get().Enqueue([this] { Tick(); }))
            m_LoopQueued.store(false, std::memory_order_release);
    }

    void VehicleConvenienceService::ApplyKeepFixed(int vehicle) noexcept
    {
        if (vehicle == 0)
            return;

        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetVehicleFixed, vehicle));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetVehicleEngineHealth, vehicle, 1000.0f));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetVehiclePetrolTankHealth, vehicle, 1000.0f));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetVehicleDirtLevel, vehicle, 0.0f));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::RemoveDecalsFromVehicle, vehicle));
        static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ForceEntityAiAndAnimationUpdate, vehicle));
    }

    void VehicleConvenienceService::ApplySeatbelt(int ped, bool enabled) noexcept
    {
        if (ped == 0)
            return;

        static_cast<void>(NativeInvoker::InvokeVoid(
            NativeId::SetPedConfigFlag,
            ped,
            WillFlyThroughWindscreenFlag,
            std::int32_t{enabled ? 0 : 1}));
        static_cast<void>(NativeInvoker::InvokeVoid(
            NativeId::SetPedCanBeKnockedOffVehicle,
            ped,
            enabled ? KnockOffNever : KnockOffDefault));
    }

    void VehicleConvenienceService::RestoreSeatbelt() noexcept
    {
        if (m_LastSeatbeltPed != 0 && EntityExists(m_LastSeatbeltPed))
            ApplySeatbelt(m_LastSeatbeltPed, false);
        m_LastSeatbeltPed = 0;
    }

    void VehicleConvenienceService::RestoreEngineState() noexcept
    {
        if (m_LastEnginePed != 0 && EntityExists(m_LastEnginePed))
        {
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetPedConfigFlag,
                m_LastEnginePed,
                LeaveEngineOnWhenExitingVehiclesFlag,
                std::int32_t{0}));
        }
        m_LastEnginePed = 0;
    }

    void VehicleConvenienceService::RestoreLights() noexcept
    {
        if (m_LastLightsVehicle != 0 && EntityExists(m_LastLightsVehicle))
        {
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetVehicleFullbeam,
                m_LastLightsVehicle,
                std::int32_t{0}));
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetVehicleLights,
                m_LastLightsVehicle,
                LightsNormal));
        }
        m_LastLightsVehicle = 0;
    }

    void VehicleConvenienceService::RestoreStance() noexcept
    {
        if (m_LastStanceVehicle != 0 && EntityExists(m_LastStanceVehicle))
        {
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetReduceDriftVehicleSuspension,
                m_LastStanceVehicle,
                std::int32_t{0}));
        }
        m_LastStanceVehicle = 0;
    }

    void VehicleConvenienceService::Tick() noexcept
    {
        if (!IsReady() || !HasPersistentWork())
        {
            RestoreSeatbelt();
            RestoreEngineState();
            RestoreLights();
            RestoreStance();
            m_SpeedMps.store(0.0f, std::memory_order_release);
            m_LoopQueued.store(false, std::memory_order_release);
            return;
        }

        const int ped = CurrentPed();
        const int vehicle = CurrentVehicle();

        {
            std::scoped_lock lock(m_Mutex);
            m_CurrentVehicleView = vehicle;
        }

        if (m_KeepFixed.load(std::memory_order_acquire) && vehicle != 0)
            ApplyKeepFixed(vehicle);

        if (m_Seatbelt.load(std::memory_order_acquire) && ped != 0)
        {
            if (m_LastSeatbeltPed != 0 && m_LastSeatbeltPed != ped)
                RestoreSeatbelt();
            ApplySeatbelt(ped, true);
            m_LastSeatbeltPed = ped;
        }
        else
        {
            RestoreSeatbelt();
        }

        if (m_KeepEngineRunning.load(std::memory_order_acquire) && ped != 0)
        {
            if (m_LastEnginePed != 0 && m_LastEnginePed != ped)
                RestoreEngineState();

            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetPedConfigFlag,
                ped,
                LeaveEngineOnWhenExitingVehiclesFlag,
                std::int32_t{1}));
            if (vehicle != 0)
            {
                static_cast<void>(NativeInvoker::InvokeVoid(
                    NativeId::SetVehicleEngineOn,
                    vehicle,
                    std::int32_t{1},
                    std::int32_t{1},
                    std::int32_t{0}));
            }
            m_LastEnginePed = ped;
        }
        else
        {
            RestoreEngineState();
        }

        if (m_AllowHats.load(std::memory_order_acquire) && ped != 0)
        {
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetPedResetFlag,
                ped,
                KeepHatInVehicleResetFlag,
                std::int32_t{1}));
        }

        const bool forceLights = m_KeepHeadlightsOn.load(std::memory_order_acquire);
        const bool highBeams = m_HighBeams.load(std::memory_order_acquire);
        if ((forceLights || highBeams) && vehicle != 0)
        {
            if (m_LastLightsVehicle != 0 && m_LastLightsVehicle != vehicle)
                RestoreLights();

            if (forceLights)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetVehicleLights, vehicle, LightsForcedOn));

            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetVehicleFullbeam,
                vehicle,
                std::int32_t{highBeams ? 1 : 0}));
            m_LastLightsVehicle = vehicle;
        }
        else
        {
            RestoreLights();
        }

        if (m_SpeedReadout.load(std::memory_order_acquire) && vehicle != 0)
        {
            const auto speed = NativeInvoker::Invoke<float>(NativeId::GetEntitySpeed, vehicle);
            m_SpeedMps.store(speed && std::isfinite(*speed) ? *speed : 0.0f, std::memory_order_release);
        }
        else
        {
            m_SpeedMps.store(0.0f, std::memory_order_release);
        }

        if (m_LoweredStance.load(std::memory_order_acquire) && vehicle != 0)
        {
            if (m_LastStanceVehicle != 0 && m_LastStanceVehicle != vehicle)
                RestoreStance();

            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetReduceDriftVehicleSuspension,
                vehicle,
                std::int32_t{1}));
            m_LastStanceVehicle = vehicle;
        }
        else
        {
            RestoreStance();
        }

        if (!Game::GameRuntime::Get().Enqueue([this] { Tick(); }))
            m_LoopQueued.store(false, std::memory_order_release);
    }

    void VehicleConvenienceService::SetKeepFixed(bool enabled) noexcept
    {
        m_KeepFixed.store(enabled, std::memory_order_release);
        if (enabled) EnsureLoop();
    }

    void VehicleConvenienceService::SetSeatbelt(bool enabled) noexcept
    {
        m_Seatbelt.store(enabled, std::memory_order_release);
        if (enabled) EnsureLoop();
        else if (Game::GameRuntime::Get().NativeReady())
            static_cast<void>(Game::GameRuntime::Get().Enqueue([this] { RestoreSeatbelt(); }));
    }

    void VehicleConvenienceService::SetKeepEngineRunning(bool enabled) noexcept
    {
        m_KeepEngineRunning.store(enabled, std::memory_order_release);
        if (enabled) EnsureLoop();
        else if (Game::GameRuntime::Get().NativeReady())
            static_cast<void>(Game::GameRuntime::Get().Enqueue([this] { RestoreEngineState(); }));
    }

    void VehicleConvenienceService::SetKeepHeadlightsOn(bool enabled) noexcept
    {
        m_KeepHeadlightsOn.store(enabled, std::memory_order_release);
        if (enabled) EnsureLoop();
        else if (!m_HighBeams.load(std::memory_order_acquire)
            && Game::GameRuntime::Get().NativeReady())
            static_cast<void>(Game::GameRuntime::Get().Enqueue([this] { RestoreLights(); }));
    }

    void VehicleConvenienceService::SetHighBeams(bool enabled) noexcept
    {
        m_HighBeams.store(enabled, std::memory_order_release);
        if (enabled) EnsureLoop();
        else if (!m_KeepHeadlightsOn.load(std::memory_order_acquire)
            && Game::GameRuntime::Get().NativeReady())
            static_cast<void>(Game::GameRuntime::Get().Enqueue([this] { RestoreLights(); }));
    }

    void VehicleConvenienceService::SetAllowHats(bool enabled) noexcept
    {
        m_AllowHats.store(enabled, std::memory_order_release);
        if (enabled) EnsureLoop();
    }

    void VehicleConvenienceService::SetSpeedReadout(bool enabled) noexcept
    {
        m_SpeedReadout.store(enabled, std::memory_order_release);
        if (enabled) EnsureLoop();
        else m_SpeedMps.store(0.0f, std::memory_order_release);
    }

    void VehicleConvenienceService::SetLoweredStance(bool enabled) noexcept
    {
        m_LoweredStance.store(enabled, std::memory_order_release);
        if (enabled) EnsureLoop();
        else if (Game::GameRuntime::Get().NativeReady())
            static_cast<void>(Game::GameRuntime::Get().Enqueue([this] { RestoreStance(); }));
    }

    bool VehicleConvenienceService::QueueAction(
        std::string pendingMessage,
        std::function<void()> action) noexcept
    {
        if (!IsReady())
            return false;

        bool expected = false;
        if (!m_ActionPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return false;

        {
            std::scoped_lock lock(m_Mutex);
            m_HaveResult = false;
            m_LastSucceeded = false;
            m_Message = std::move(pendingMessage);
        }

        if (Game::GameRuntime::Get().Enqueue(std::move(action)))
            return true;

        m_ActionPending.store(false, std::memory_order_release);
        return false;
    }

    void VehicleConvenienceService::FinishAction(bool success, std::string message) noexcept
    {
        {
            std::scoped_lock lock(m_Mutex);
            m_HaveResult = true;
            m_LastSucceeded = success;
            m_Message = std::move(message);
        }
        m_ActionPending.store(false, std::memory_order_release);
    }

    bool VehicleConvenienceService::QueueSetEngine(bool enabled) noexcept
    {
        return QueueAction(enabled ? "Turning engine on..." : "Turning engine off...", [this, enabled] {
            const int vehicle = CurrentVehicle();
            if (vehicle == 0)
            {
                FinishAction(false, "Enter a vehicle first");
                return;
            }

            const bool ok = NativeInvoker::InvokeVoid(
                NativeId::SetVehicleEngineOn,
                vehicle,
                std::int32_t{enabled ? 1 : 0},
                std::int32_t{1},
                std::int32_t{enabled ? 0 : 1});
            FinishAction(ok, ok ? (enabled ? "Engine on" : "Engine off") : "Engine request failed");
        });
    }

    bool VehicleConvenienceService::QueueSetDoorsLocked(bool locked) noexcept
    {
        return QueueAction(locked ? "Locking vehicle..." : "Unlocking vehicle...", [this, locked] {
            const int vehicle = CurrentVehicle();
            if (vehicle == 0)
            {
                FinishAction(false, "Enter a vehicle first");
                return;
            }

            const bool ok = NativeInvoker::InvokeVoid(
                NativeId::SetVehicleDoorsLocked,
                vehicle,
                locked ? 2 : 1);
            FinishAction(ok, ok ? (locked ? "Vehicle locked" : "Vehicle unlocked") : "Door-lock request failed");
        });
    }

    bool VehicleConvenienceService::QueueEnterLastVehicle() noexcept
    {
        return QueueAction("Entering last vehicle...", [this] {
            const int ped = CurrentPed();
            if (ped == 0)
            {
                FinishAction(false, "Local player ped is unavailable");
                return;
            }

            const auto current = NativeInvoker::Invoke<std::int32_t>(
                NativeId::IsPedInAnyVehicle,
                ped,
                std::int32_t{0});
            if (!current || *current != 0)
            {
                FinishAction(false, "Exit the current vehicle first");
                return;
            }

            const auto vehicle = NativeInvoker::Invoke<std::int32_t>(
                NativeId::GetVehiclePedIsIn,
                ped,
                std::int32_t{1});
            if (!vehicle || *vehicle == 0 || !EntityExists(*vehicle))
            {
                FinishAction(false, "GTA has no usable last occupied vehicle");
                return;
            }

            const auto driver = NativeInvoker::Invoke<std::int32_t>(
                NativeId::GetPedInVehicleSeat,
                *vehicle,
                -1,
                std::int32_t{0});
            if (!driver || (*driver != 0 && *driver != ped))
            {
                FinishAction(false, "The last vehicle driver seat is occupied");
                return;
            }

            const bool ok = NativeInvoker::InvokeVoid(
                NativeId::SetPedIntoVehicle,
                ped,
                *vehicle,
                -1);
            FinishAction(ok, ok ? "Entered last vehicle" : "Enter-last-vehicle request failed");
        });
    }
}
