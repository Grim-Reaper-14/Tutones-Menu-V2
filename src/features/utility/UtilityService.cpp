#include "UtilityService.hpp"

#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"

namespace TutonesV2::Features::Utility
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;
    }

    UtilityService& UtilityService::Get() noexcept
    {
        static UtilityService instance;
        return instance;
    }

    bool UtilityService::Initialize() noexcept
    {
        m_LoopQueued.store(false, std::memory_order_release);
        m_ShowCoordinates.store(false, std::memory_order_release);
        m_ShowHeading.store(false, std::memory_order_release);
        m_ShowFps.store(false, std::memory_order_release);
        m_ShowSessionInfo.store(false, std::memory_order_release);
        m_DisableCameraShake.store(false, std::memory_order_release);
        {
            std::scoped_lock lock(m_Mutex);
            m_PositionReadable = false;
            m_Position = {};
            m_Heading = 0.0f;
        }
        m_Ready.store(true, std::memory_order_release);
        return true;
    }

    void UtilityService::Shutdown() noexcept
    {
        m_Ready.store(false, std::memory_order_release);
        m_LoopQueued.store(false, std::memory_order_release);
        m_DisableCameraShake.store(false, std::memory_order_release);
    }

    bool UtilityService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire);
    }

    UtilitySnapshot UtilityService::Snapshot() const
    {
        UtilitySnapshot snapshot{};
        snapshot.ready = IsReady();
        snapshot.showCoordinates = m_ShowCoordinates.load(std::memory_order_acquire);
        snapshot.showHeading = m_ShowHeading.load(std::memory_order_acquire);
        snapshot.showFps = m_ShowFps.load(std::memory_order_acquire);
        snapshot.showSessionInfo = m_ShowSessionInfo.load(std::memory_order_acquire);
        snapshot.disableCameraShake = m_DisableCameraShake.load(std::memory_order_acquire);

        std::scoped_lock lock(m_Mutex);
        snapshot.positionReadable = m_PositionReadable;
        snapshot.position = m_Position;
        snapshot.heading = m_Heading;
        return snapshot;
    }

    void UtilityService::SetShowCoordinates(bool enabled) noexcept
    {
        m_ShowCoordinates.store(enabled, std::memory_order_release);
        if (enabled)
            EnsureLoop();
    }

    void UtilityService::SetShowHeading(bool enabled) noexcept
    {
        m_ShowHeading.store(enabled, std::memory_order_release);
        if (enabled)
            EnsureLoop();
    }

    void UtilityService::SetShowFps(bool enabled) noexcept
    {
        m_ShowFps.store(enabled, std::memory_order_release);
    }

    void UtilityService::SetShowSessionInfo(bool enabled) noexcept
    {
        m_ShowSessionInfo.store(enabled, std::memory_order_release);
    }

    void UtilityService::SetDisableCameraShake(bool enabled) noexcept
    {
        m_DisableCameraShake.store(enabled, std::memory_order_release);
        if (enabled)
            EnsureLoop();
    }

    bool UtilityService::AnyOverlayVisible() const noexcept
    {
        return m_ShowCoordinates.load(std::memory_order_acquire)
            || m_ShowHeading.load(std::memory_order_acquire)
            || m_ShowFps.load(std::memory_order_acquire)
            || m_ShowSessionInfo.load(std::memory_order_acquire);
    }

    void UtilityService::Maintain() noexcept
    {
        if (NeedsGameLoop())
            EnsureLoop();
    }

    bool UtilityService::NeedsGameLoop() const noexcept
    {
        return m_ShowCoordinates.load(std::memory_order_acquire)
            || m_ShowHeading.load(std::memory_order_acquire)
            || m_DisableCameraShake.load(std::memory_order_acquire);
    }

    void UtilityService::EnsureLoop() noexcept
    {
        if (!IsReady() || !Game::GameRuntime::Get().NativeReady() || !NeedsGameLoop())
            return;

        bool expected = false;
        if (!m_LoopQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        if (!Game::GameRuntime::Get().Enqueue([this] { Tick(); }))
            m_LoopQueued.store(false, std::memory_order_release);
    }

    void UtilityService::Tick() noexcept
    {
        if (!IsReady() || !NeedsGameLoop())
        {
            m_LoopQueued.store(false, std::memory_order_release);
            return;
        }

        if (m_DisableCameraShake.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::StopGameplayCamShaking, std::int32_t{1}));

        if (m_ShowCoordinates.load(std::memory_order_acquire)
            || m_ShowHeading.load(std::memory_order_acquire))
        {
            const auto ped = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
            if (ped && *ped != 0)
            {
                const auto coords = NativeInvoker::Invoke<Game::Native::NativeVector3>(
                    NativeId::GetEntityCoords,
                    *ped,
                    std::int32_t{0});
                const auto heading = NativeInvoker::Invoke<float>(
                    NativeId::GetEntityHeading,
                    *ped);

                std::scoped_lock lock(m_Mutex);
                m_PositionReadable = coords.has_value();
                if (coords)
                    m_Position = *coords;
                if (heading)
                    m_Heading = *heading;
            }
        }

        if (!Game::GameRuntime::Get().Enqueue([this] { Tick(); }))
            m_LoopQueued.store(false, std::memory_order_release);
    }
}
