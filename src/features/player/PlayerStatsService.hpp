#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace TutonesV2::Features::Player
{
    struct PlayerStatsSnapshot final
    {
        bool pending{};
        bool readable{};
        bool lastSucceeded{};
        int characterIndex{-1};
        int rank{};
        int rp{};
        int kills{};
        int deaths{};
        float kdRatio{};
        std::uint64_t revision{};
        std::string message{"Ready"};
    };

    class PlayerStatsService final
    {
    public:
        static PlayerStatsService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] PlayerStatsSnapshot Snapshot() const;

        bool QueueRefresh() noexcept;
        bool QueueApply(int rank, int rp, int kills, int deaths) noexcept;

        [[nodiscard]] static std::optional<int> RpForRank(int rank) noexcept;

    private:
        PlayerStatsService() = default;

        [[nodiscard]] static std::uint32_t Joaat(std::string value) noexcept;
        [[nodiscard]] static bool ResolveCharacterStat(std::string& name, int characterIndex) noexcept;
        [[nodiscard]] static bool ReadStat(const char* name, int characterIndex, int& value) noexcept;
        [[nodiscard]] static bool WriteStat(const char* name, int characterIndex, int value) noexcept;

        void RefreshOnGameThread(const char* message, bool operationSucceeded) noexcept;
        void PublishPending(const char* message);
        void PublishFailure(const char* message);

        std::atomic_bool m_Ready{};
        std::atomic_bool m_Pending{};
        mutable std::mutex m_Mutex;
        PlayerStatsSnapshot m_Snapshot{};
        std::uint64_t m_Revision{};
    };
}
