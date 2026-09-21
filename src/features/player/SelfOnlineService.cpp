#include "SelfOnlineService.hpp"

#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"
#include "../../game/native/NativePointers.hpp"
#include "../../game/script/ScriptGlobal.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

namespace TutonesV2::Features::Player
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;
        using Game::Script::ScriptGlobal;

        constexpr std::uint32_t FreemodeHash = 0xC875557Du;
        constexpr std::size_t GlobalPlayerBd = 2658296;
        constexpr std::size_t GlobalPlayerEntrySize = 468;
        constexpr std::size_t FreemodeStateOffset = 0;
        constexpr std::size_t OffRadarActiveOffset = 214;
        constexpr std::size_t OffRadarNetworkTimeGlobal = 2673276;
        constexpr std::size_t OffRadarNetworkTimeOffset = 58;
        constexpr std::int32_t FreemodeRunning = 4;
        constexpr std::size_t FreemodeGlobal = 2733190;
        constexpr std::size_t GhostOrganizationFlagsOffset = 3758;
        constexpr std::int32_t GhostOrganizationMask = 1 << 2;

        [[nodiscard]] int PlayerId() noexcept
        {
            const auto value = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerId);
            return value ? *value : -1;
        }
    }

    SelfOnlineService& SelfOnlineService::Get() noexcept
    {
        static SelfOnlineService instance;
        return instance;
    }

    bool SelfOnlineService::Initialize() noexcept
    {
        m_LoopQueued.store(false, std::memory_order_release);
        m_OffRadarApplied.store(false, std::memory_order_release);
        m_GhostApplied.store(false, std::memory_order_release);
        m_Mode.store(RadarMode::Off, std::memory_order_release);
        m_Ready.store(true, std::memory_order_release);
        Publish({});
        return true;
    }

    void SelfOnlineService::Shutdown() noexcept
    {
        if (!m_Ready.exchange(false, std::memory_order_acq_rel))
            return;

        m_Mode.store(RadarMode::Off, std::memory_order_release);
        m_LoopQueued.store(false, std::memory_order_release);

        if (!m_OffRadarApplied.load(std::memory_order_acquire)
            && !m_GhostApplied.load(std::memory_order_acquire))
        {
            return;
        }

        auto& runtime = Game::GameRuntime::Get();
        if (!runtime.NativeReady())
            return;

        const auto completed = std::make_shared<std::atomic_bool>(false);
        if (!runtime.Enqueue([this, completed] {
                Apply(RadarMode::Off);
                completed->store(true, std::memory_order_release);
            }))
        {
            return;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while (!completed->load(std::memory_order_acquire)
            && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool SelfOnlineService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire);
    }

    RadarMode SelfOnlineService::Mode() const noexcept
    {
        return m_Mode.load(std::memory_order_acquire);
    }

    SelfOnlineSnapshot SelfOnlineService::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    bool SelfOnlineService::SetMode(RadarMode mode) noexcept
    {
        if (!IsReady() || !Game::GameRuntime::Get().NativeReady())
            return false;

        const auto previous = m_Mode.exchange(mode, std::memory_order_acq_rel);
        if (previous == mode)
            return true;

        if (mode != RadarMode::Off)
        {
            if (EnsureLoop())
                return true;

            m_Mode.store(previous, std::memory_order_release);
            return false;
        }

        if (Game::GameRuntime::Get().Enqueue([this] { Apply(RadarMode::Off); }))
            return true;

        m_Mode.store(previous, std::memory_order_release);
        return false;
    }

    bool SelfOnlineService::EnsureLoop() noexcept
    {
        bool expected = false;
        if (!m_LoopQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return true;

        if (Game::GameRuntime::Get().Enqueue([this] { Tick(); }))
            return true;

        m_LoopQueued.store(false, std::memory_order_release);
        return false;
    }

    void SelfOnlineService::Tick() noexcept
    {
        if (!IsReady())
        {
            m_LoopQueued.store(false, std::memory_order_release);
            return;
        }

        Apply(Mode());

        if (Mode() == RadarMode::Off
            && !m_OffRadarApplied.load(std::memory_order_acquire)
            && !m_GhostApplied.load(std::memory_order_acquire))
        {
            m_LoopQueued.store(false, std::memory_order_release);
            return;
        }

        if (!Game::GameRuntime::Get().Enqueue([this] { Tick(); }))
            m_LoopQueued.store(false, std::memory_order_release);
    }

    bool SelfOnlineService::FreemodeThreadReady() const noexcept
    {
        const auto* threads = Game::Native::NativePointers::Get().ScriptThreads();
        if (!threads || !threads->data || threads->size == 0 || threads->size > threads->capacity)
            return false;

        for (std::uint16_t index = 0; index < threads->size; ++index)
        {
            const auto* thread = threads->data[index];
            if (thread && thread->context.threadId != 0 && thread->scriptHash == FreemodeHash)
                return true;
        }
        return false;
    }

    void SelfOnlineService::Apply(RadarMode mode) noexcept
    {
        SelfOnlineSnapshot state{};
        state.mode = mode;

        auto& pointers = Game::Native::NativePointers::Get();
        auto** globals = pointers.ScriptGlobals();
        auto* sessionStarted = pointers.IsSessionStarted();
        auto* networkTime = pointers.NetworkTime();

        state.globalsReady = globals != nullptr;
        state.sessionStarted = sessionStarted && *sessionStarted;
        state.freemodeReady = FreemodeThreadReady();

        const int player = PlayerId();
        if (!globals || !sessionStarted || !networkTime || player < 0 || player >= 32)
        {
            if (!state.sessionStarted)
            {
                m_OffRadarApplied.store(false, std::memory_order_release);
                m_GhostApplied.store(false, std::memory_order_release);
            }
            state.offRadarApplied = m_OffRadarApplied.load(std::memory_order_acquire);
            state.ghostOrganizationApplied = m_GhostApplied.load(std::memory_order_acquire);
            state.message = "Online globals/session are not ready";
            Publish(std::move(state));
            return;
        }

        const auto playerEntry = ScriptGlobal(GlobalPlayerBd)
            .At(static_cast<std::size_t>(player), GlobalPlayerEntrySize);
        auto* freemodeState = playerEntry.At(FreemodeStateOffset).As<std::int32_t>(globals);
        auto* offRadarActive = playerEntry.At(OffRadarActiveOffset).As<std::int32_t>(globals);
        auto* networkTimeGlobal = ScriptGlobal(OffRadarNetworkTimeGlobal)
            .At(OffRadarNetworkTimeOffset).As<std::int32_t>(globals);
        auto* ghostFlags = ScriptGlobal(FreemodeGlobal)
            .At(GhostOrganizationFlagsOffset).As<std::int32_t>(globals);

        state.safeToModify =
            state.sessionStarted
            && state.freemodeReady
            && freemodeState
            && *freemodeState == FreemodeRunning
            && offRadarActive
            && networkTimeGlobal
            && ghostFlags;

        if (!state.safeToModify)
        {
            state.offRadarApplied = m_OffRadarApplied.load(std::memory_order_acquire);
            state.ghostOrganizationApplied = m_GhostApplied.load(std::memory_order_acquire);
            state.message = "Waiting for a safe GTA Online freemode context";
            Publish(std::move(state));
            return;
        }

        if (mode == RadarMode::Off)
        {
            if (m_GhostApplied.load(std::memory_order_acquire))
                *ghostFlags &= ~GhostOrganizationMask;
            if (m_OffRadarApplied.load(std::memory_order_acquire))
                *offRadarActive = 0;

            m_GhostApplied.store(false, std::memory_order_release);
            m_OffRadarApplied.store(false, std::memory_order_release);
            state.message = "Off Radar / Ghost Organization disabled";
        }
        else
        {
            *networkTimeGlobal = static_cast<std::int32_t>(*networkTime);
            *offRadarActive = 1;
            m_OffRadarApplied.store(true, std::memory_order_release);

            if (mode == RadarMode::GhostOrganization)
            {
                *ghostFlags |= GhostOrganizationMask;
                m_GhostApplied.store(true, std::memory_order_release);
                state.message = "Ghost Organization active";
            }
            else
            {
                if (m_GhostApplied.load(std::memory_order_acquire))
                    *ghostFlags &= ~GhostOrganizationMask;
                m_GhostApplied.store(false, std::memory_order_release);
                state.message = "Off Radar active";
            }
        }

        state.offRadarApplied = m_OffRadarApplied.load(std::memory_order_acquire);
        state.ghostOrganizationApplied = m_GhostApplied.load(std::memory_order_acquire);
        Publish(std::move(state));
    }

    void SelfOnlineService::Publish(SelfOnlineSnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = std::move(snapshot);
    }
}
