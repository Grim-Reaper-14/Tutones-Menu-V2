#include "OnlineSessionService.hpp"

#include "../../core/Logger.hpp"
#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativePointers.hpp"
#include "../../game/script/ScriptFunction.hpp"
#include "../../game/script/ScriptGlobal.hpp"
#include "../../game/script/ScriptPointer.hpp"
#include "../../game/script/ScriptRuntime.hpp"

#include <cstdint>
#include <utility>

namespace TutonesV2::Features::Online
{
    namespace
    {
        constexpr std::uint32_t Joaat(const char* text) noexcept
        {
            std::uint32_t hash{};
            while (text && *text)
            {
                char ch = *text++;
                if (ch >= 'A' && ch <= 'Z')
                    ch = static_cast<char>(ch - 'A' + 'a');
                hash += static_cast<std::uint8_t>(ch);
                hash += hash << 10;
                hash ^= hash >> 6;
            }
            hash += hash << 3;
            hash ^= hash >> 11;
            hash += hash << 15;
            return hash;
        }

        constexpr std::uint32_t ShopControllerHash = Joaat("shop_controller");
        constexpr std::size_t JoinTypeGlobal = 1575048;
    }

    OnlineSessionService& OnlineSessionService::Get() noexcept
    {
        static OnlineSessionService instance;
        return instance;
    }

    bool OnlineSessionService::Initialize() noexcept
    {
        m_Pending.store(false, std::memory_order_release);
        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot = {};
            m_Snapshot.ready = true;
            m_Snapshot.supported = Supported();
            m_Snapshot.message = m_Snapshot.supported
                ? "V1 Online session switcher ready"
                : "Waiting for optional ScriptPrograms / ScriptVM pointers";
        }
        m_Ready.store(true, std::memory_order_release);
        return true;
    }

    void OnlineSessionService::Shutdown() noexcept
    {
        m_Ready.store(false, std::memory_order_release);
        m_Pending.store(false, std::memory_order_release);
    }

    bool OnlineSessionService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire);
    }

    bool OnlineSessionService::Supported() const noexcept
    {
        auto& pointers = Game::Native::NativePointers::Get();
        return pointers.ScriptThreads()
            && pointers.ScriptGlobals()
            && pointers.ScriptPrograms()
            && pointers.ScriptVm();
    }

    OnlineSessionSnapshot OnlineSessionService::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        auto out = m_Snapshot;
        out.ready = IsReady();
        out.supported = Supported();
        out.pending = m_Pending.load(std::memory_order_acquire);
        return out;
    }

    bool OnlineSessionService::QueueJoin(OnlineJoinType type) noexcept
    {
        if (!IsReady()
            || !Supported()
            || !Game::GameRuntime::Get().NativeReady())
        {
            return false;
        }

        bool expected = false;
        if (!m_Pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return false;

        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot.haveResult = false;
            m_Snapshot.lastSucceeded = false;
            m_Snapshot.lastRequested = type;
            m_Snapshot.message = type == OnlineJoinType::LeaveOnline
                ? "Leave Online transition queued"
                : "Online session transition queued";
        }

        if (Game::GameRuntime::Get().Enqueue([this, type] { Execute(type); }))
            return true;

        Finish(type, false, false, "GTA scheduler queue unavailable");
        return false;
    }

    bool OnlineSessionService::QueueLeaveOnline() noexcept
    {
        return QueueJoin(OnlineJoinType::LeaveOnline);
    }

    void OnlineSessionService::Execute(OnlineJoinType type) noexcept
    {
        auto& scripts = Game::Script::ScriptRuntime::Get();
        auto** globals = scripts.Globals();
        auto* thread = scripts.FindThread(ShopControllerHash);
        auto* program = scripts.FindProgram(ShopControllerHash);
        auto* joinType = globals
            ? Game::Script::ScriptGlobal(JoinTypeGlobal).As<std::int32_t>(globals)
            : nullptr;

        if (!scripts.IsReady()
            || !scripts.ScriptVm()
            || !thread
            || !program
            || !joinType)
        {
            Core::Logger::Get().Warn(
                "online.session",
                "shop_controller session-transition prerequisites are unavailable");
            Finish(
                type,
                false,
                false,
                "shop_controller is not ready for a session transition");
            return;
        }

        static Game::Script::ScriptFunction sendToClouds(
            ShopControllerHash,
            Game::Script::ScriptPointer(
                "SendToClouds",
                "2D 00 02 00 00 72 5D ? ? ? 72"));

        if (!sendToClouds.CallVoidOnThread(thread))
        {
            Core::Logger::Get().Warn(
                "online.session",
                "SendToClouds script function could not be resolved or executed");
            Finish(
                type,
                true,
                false,
                "SendToClouds could not be resolved in shop_controller");
            return;
        }

        *joinType = static_cast<std::int32_t>(type);
        const bool verified = *joinType == static_cast<std::int32_t>(type);

        Core::Logger::Get().Info(
            "online.session",
            type == OnlineJoinType::LeaveOnline
                ? "Leave Online transition launched"
                : "GTA Online session transition launched");

        Finish(
            type,
            true,
            verified,
            verified
                ? (type == OnlineJoinType::LeaveOnline
                    ? "Leave Online transition launched"
                    : "Online session transition launched")
                : "Session type global failed read-back verification");
    }

    void OnlineSessionService::Finish(
        OnlineJoinType type,
        bool shopControllerReady,
        bool success,
        std::string message) noexcept
    {
        {
            std::scoped_lock lock(m_Mutex);
            m_Snapshot.ready = IsReady();
            m_Snapshot.supported = Supported();
            m_Snapshot.pending = false;
            m_Snapshot.haveResult = true;
            m_Snapshot.lastSucceeded = success;
            m_Snapshot.shopControllerReady = shopControllerReady;
            m_Snapshot.lastRequested = type;
            m_Snapshot.message = std::move(message);
        }
        m_Pending.store(false, std::memory_order_release);
    }
}
