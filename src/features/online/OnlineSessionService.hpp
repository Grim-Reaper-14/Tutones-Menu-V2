#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace TutonesV2::Features::Online
{
    enum class OnlineJoinType : int
    {
        Public = 0,
        NewPublic = 1,
        ClosedCrew = 2,
        Crew = 3,
        ClosedFriends = 6,
        FindFriend = 9,
        Solo = 10,
        InviteOnly = 11,
        JoinCrew = 12,
        Sctv = 13,
        LeaveOnline = -1,
    };

    struct OnlineJoinTypeEntry final
    {
        OnlineJoinType value{};
        const char* label{};
    };

    inline constexpr std::array<OnlineJoinTypeEntry, 10> OnlineJoinTypes{{
        {OnlineJoinType::Public, "Public"},
        {OnlineJoinType::NewPublic, "New / Solo Public"},
        {OnlineJoinType::ClosedCrew, "Closed Crew"},
        {OnlineJoinType::Crew, "Crew"},
        {OnlineJoinType::ClosedFriends, "Closed Friends"},
        {OnlineJoinType::FindFriend, "Find Friend"},
        {OnlineJoinType::Solo, "Solo"},
        {OnlineJoinType::InviteOnly, "Invite Only"},
        {OnlineJoinType::JoinCrew, "Join Crew"},
        {OnlineJoinType::Sctv, "SCTV"},
    }};

    struct OnlineSessionSnapshot final
    {
        bool ready{};
        bool supported{};
        bool pending{};
        bool haveResult{};
        bool lastSucceeded{};
        bool shopControllerReady{};
        OnlineJoinType lastRequested{OnlineJoinType::Public};
        std::string message{"Ready"};
    };

    class OnlineSessionService final
    {
    public:
        static OnlineSessionService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] bool Supported() const noexcept;
        [[nodiscard]] OnlineSessionSnapshot Snapshot() const;

        bool QueueJoin(OnlineJoinType type) noexcept;
        bool QueueLeaveOnline() noexcept;

    private:
        OnlineSessionService() = default;

        void Execute(OnlineJoinType type) noexcept;
        void Finish(
            OnlineJoinType type,
            bool shopControllerReady,
            bool success,
            std::string message) noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_Pending{};
        mutable std::mutex m_Mutex;
        OnlineSessionSnapshot m_Snapshot{};
    };
}
