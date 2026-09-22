#include "OnlineStatusService.hpp"

#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"
#include "../../game/native/NativePointers.hpp"

#include <chrono>

namespace TutonesV2::Features::Online
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;

        constexpr std::uint32_t FreemodeHash = 0xC875557Du;

        std::uint64_t NowMs() noexcept
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(
                duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        }
    }

    OnlineStatusService& OnlineStatusService::Get() noexcept
    {
        static OnlineStatusService instance;
        return instance;
    }

    bool OnlineStatusService::Initialize() noexcept
    {
        m_RefreshPending.store(false, std::memory_order_release);
        m_LastRefreshMs.store(0, std::memory_order_release);
        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot = {};
            m_Snapshot.ready = true;
        }
        m_Ready.store(true, std::memory_order_release);
        return true;
    }

    void OnlineStatusService::Shutdown() noexcept
    {
        m_Ready.store(false, std::memory_order_release);
        m_RefreshPending.store(false, std::memory_order_release);
    }

    bool OnlineStatusService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire);
    }

    OnlineStatusSnapshot OnlineStatusService::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        auto snapshot = m_Snapshot;
        snapshot.ready = IsReady();
        snapshot.refreshPending = m_RefreshPending.load(std::memory_order_acquire);
        return snapshot;
    }

    void OnlineStatusService::RequestRefresh() noexcept
    {
        if (!IsReady() || !Game::GameRuntime::Get().NativeReady())
            return;

        const auto now = NowMs();
        const auto last = m_LastRefreshMs.load(std::memory_order_acquire);
        if (last != 0 && now - last < 750)
            return;

        bool expected = false;
        if (!m_RefreshPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        m_LastRefreshMs.store(now, std::memory_order_release);
        if (!Game::GameRuntime::Get().Enqueue([this] { RefreshOnGameThread(); }))
            m_RefreshPending.store(false, std::memory_order_release);
    }

    void OnlineStatusService::RefreshOnGameThread() noexcept
    {
        OnlineStatusSnapshot snapshot{};
        snapshot.ready = true;

        auto& pointers = Game::Native::NativePointers::Get();
        auto** globals = pointers.ScriptGlobals();
        auto* session = pointers.IsSessionStarted();
        auto* networkTime = pointers.NetworkTime();
        auto* threads = pointers.ScriptThreads();

        snapshot.globalsReady = globals != nullptr;
        snapshot.sessionStarted = session && *session;
        snapshot.networkTimeReady = networkTime != nullptr;
        if (networkTime)
            snapshot.networkTime = *networkTime;

        const auto player = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerId);
        if (player)
            snapshot.localPlayer = *player;

        if (threads && threads->data && threads->size <= threads->capacity)
        {
            snapshot.scriptThreadCount = static_cast<int>(threads->size);
            for (std::uint16_t index = 0; index < threads->size; ++index)
            {
                const auto* thread = threads->data[index];
                if (!thread || thread->context.threadId == 0)
                    continue;

                ++snapshot.activeScriptThreads;
                if (thread->scriptHash == FreemodeHash)
                    snapshot.freemodeReady = true;
            }
        }

        if (!snapshot.sessionStarted)
            snapshot.message = "GTA Online session is not active";
        else if (!snapshot.globalsReady)
            snapshot.message = "Session active; script globals unavailable";
        else if (!snapshot.freemodeReady)
            snapshot.message = "Session active; waiting for freemode thread";
        else
            snapshot.message = "GTA Online / freemode ready";

        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot = std::move(snapshot);
        }

        m_RefreshPending.store(false, std::memory_order_release);
    }
}
