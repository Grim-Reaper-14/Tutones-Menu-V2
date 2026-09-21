#include "PlayerStatsService.hpp"

#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace TutonesV2::Features::Player
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;
    }

    PlayerStatsService& PlayerStatsService::Get() noexcept
    {
        static PlayerStatsService instance;
        return instance;
    }

    bool PlayerStatsService::Initialize() noexcept
    {
        m_Pending.store(false, std::memory_order_release);
        m_Ready.store(true, std::memory_order_release);
        std::scoped_lock lock(m_Mutex);
        m_Revision = 0;
        m_Snapshot = {};
        return true;
    }

    void PlayerStatsService::Shutdown() noexcept
    {
        m_Ready.store(false, std::memory_order_release);
        m_Pending.store(false, std::memory_order_release);
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.pending = false;
        m_Snapshot.message = "Offline";
    }

    bool PlayerStatsService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire);
    }

    PlayerStatsSnapshot PlayerStatsService::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    std::uint32_t PlayerStatsService::Joaat(std::string value) noexcept
    {
        std::uint32_t hash{};
        for (char& character : value)
        {
            if (character >= 'A' && character <= 'Z')
                character = static_cast<char>(character - 'A' + 'a');

            const auto byte = static_cast<std::uint8_t>(character);
            hash += byte;
            hash += hash << 10;
            hash ^= hash >> 6;
        }

        hash += hash << 3;
        hash ^= hash >> 11;
        hash += hash << 15;
        return hash;
    }

    bool PlayerStatsService::ResolveCharacterStat(std::string& name, int characterIndex) noexcept
    {
        if (name.size() >= 3
            && (name[0] == 'M' || name[0] == 'm')
            && (name[1] == 'P' || name[1] == 'p')
            && (name[2] == 'X' || name[2] == 'x'))
        {
            if (characterIndex < 0 || characterIndex > 9)
                return false;
            name[2] = static_cast<char>('0' + characterIndex);
        }
        return true;
    }

    bool PlayerStatsService::ReadStat(const char* name, int characterIndex, int& value) noexcept
    {
        if (!name)
            return false;

        std::string stat(name);
        if (!ResolveCharacterStat(stat, characterIndex))
            return false;

        const auto result = NativeInvoker::Invoke<std::int32_t>(
            NativeId::StatGetInt,
            Joaat(std::move(stat)),
            &value,
            -1);
        return result && *result != 0;
    }

    bool PlayerStatsService::WriteStat(const char* name, int characterIndex, int value) noexcept
    {
        if (!name)
            return false;

        std::string stat(name);
        if (!ResolveCharacterStat(stat, characterIndex))
            return false;

        const auto result = NativeInvoker::Invoke<std::int32_t>(
            NativeId::StatSetInt,
            Joaat(std::move(stat)),
            value,
            std::int32_t{1});
        return result && *result != 0;
    }

    std::optional<int> PlayerStatsService::RpForRank(int rank) noexcept
    {
        static constexpr std::array<int, 97> table{{
            0,800,2100,3800,6100,9500,12500,16000,19800,24000,28500,33400,38700,44200,50200,56400,63000,69900,77100,84700,
            92500,100700,109200,118000,127100,136500,146200,156200,166500,177100,188000,199200,210700,222400,234500,246800,259400,272300,285500,299000,
            312700,326800,341000,355600,370500,385600,401000,416600,432600,448800,465200,482000,499000,516300,533800,551600,569600,588000,606500,625400,
            644500,663800,683400,703300,723400,743800,764500,785400,806500,827900,849600,871500,893600,916000,938700,961600,984700,1008100,1031800,1055700,
            1079800,1104200,1128800,1153700,1178800,1204200,1229800,1255600,1281700,1308100,1334600,1361400,1388500,1415800,1443300,1471100,1499100
        }};

        rank = std::clamp(rank, 1, 8000);
        if (rank <= 97)
            return table[static_cast<std::size_t>(rank - 1)];

        const std::int64_t r = rank;
        const std::int64_t value = (25 * r * r) + (23575 * r) - 1023150;
        if (value < 0 || value > std::numeric_limits<int>::max())
            return std::nullopt;

        return static_cast<int>(value);
    }

    bool PlayerStatsService::QueueRefresh() noexcept
    {
        if (!IsReady() || !Game::GameRuntime::Get().NativeReady())
            return false;

        bool expected = false;
        if (!m_Pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return false;

        PublishPending("Reading current GTA Online stats...");
        if (Game::GameRuntime::Get().Enqueue([this] {
                RefreshOnGameThread("Stats refreshed", true);
            }))
        {
            return true;
        }

        PublishFailure("Game-thread queue unavailable");
        return false;
    }

    bool PlayerStatsService::QueueApply(int rank, int rp, int kills, int deaths) noexcept
    {
        if (!IsReady() || !Game::GameRuntime::Get().NativeReady())
            return false;

        rank = std::clamp(rank, 1, 8000);
        rp = std::max(0, rp);
        kills = std::max(0, kills);
        deaths = std::max(0, deaths);

        bool expected = false;
        if (!m_Pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return false;

        PublishPending("Applying Rank / RP / Kills / Deaths...");
        if (Game::GameRuntime::Get().Enqueue([this, rank, rp, kills, deaths] {
                int characterIndex = -1;
                const auto characterResult = NativeInvoker::Invoke<std::int32_t>(
                    NativeId::StatGetInt,
                    Joaat("MPPLY_LAST_MP_CHAR"),
                    &characterIndex,
                    -1);

                if (!characterResult || *characterResult == 0
                    || characterIndex < 0 || characterIndex > 1)
                {
                    PublishFailure("Could not resolve the active GTA Online character");
                    return;
                }

                bool writes = true;
                writes = WriteStat("MPPLY_GLOBALXP", -1, rp) && writes;
                writes = WriteStat("MPX_CHAR_XP_FM", characterIndex, rp) && writes;
                writes = WriteStat("MPX_CHAR_SET_RP_GIFT_ADMIN", characterIndex, rp) && writes;
                writes = WriteStat("MPX_CHAR_RANK_FM", characterIndex, rank) && writes;
                writes = WriteStat("MPPLY_KILLS_PLAYERS", -1, kills) && writes;
                writes = WriteStat("MPPLY_DEATHS_PLAYER", -1, deaths) && writes;

                int readRank{}, readRp{}, readKills{}, readDeaths{};
                const bool matches =
                    ReadStat("MPX_CHAR_RANK_FM", characterIndex, readRank)
                    && ReadStat("MPX_CHAR_XP_FM", characterIndex, readRp)
                    && ReadStat("MPPLY_KILLS_PLAYERS", -1, readKills)
                    && ReadStat("MPPLY_DEATHS_PLAYER", -1, readDeaths)
                    && readRank == rank
                    && readRp == rp
                    && readKills == kills
                    && readDeaths == deaths;

                RefreshOnGameThread(
                    writes && matches
                        ? "Rank / RP / kills / deaths applied and verified"
                        : writes
                            ? "Stat writes completed, but read-back did not fully match"
                            : "One or more stat writes were rejected",
                    writes && matches);
            }))
        {
            return true;
        }

        PublishFailure("Could not queue stat writes");
        return false;
    }

    void PlayerStatsService::RefreshOnGameThread(const char* message, bool operationSucceeded) noexcept
    {
        PlayerStatsSnapshot snapshot{};

        int characterIndex = -1;
        const auto characterResult = NativeInvoker::Invoke<std::int32_t>(
            NativeId::StatGetInt,
            Joaat("MPPLY_LAST_MP_CHAR"),
            &characterIndex,
            -1);

        if (!characterResult || *characterResult == 0
            || characterIndex < 0 || characterIndex > 1)
        {
            PublishFailure("GTA Online character stats are unavailable");
            return;
        }

        snapshot.characterIndex = characterIndex;
        snapshot.readable =
            ReadStat("MPX_CHAR_RANK_FM", characterIndex, snapshot.rank)
            && ReadStat("MPX_CHAR_XP_FM", characterIndex, snapshot.rp)
            && ReadStat("MPPLY_KILLS_PLAYERS", -1, snapshot.kills)
            && ReadStat("MPPLY_DEATHS_PLAYER", -1, snapshot.deaths);

        snapshot.kdRatio = snapshot.deaths > 0
            ? static_cast<float>(snapshot.kills) / static_cast<float>(snapshot.deaths)
            : static_cast<float>(snapshot.kills);
        snapshot.pending = false;
        snapshot.lastSucceeded = snapshot.readable && operationSucceeded;
        snapshot.message = snapshot.readable
            ? (message ? message : "Stats refreshed")
            : "One or more GTA Online stats could not be read";

        m_Pending.store(false, std::memory_order_release);
        std::scoped_lock lock(m_Mutex);
        snapshot.revision = ++m_Revision;
        m_Snapshot = std::move(snapshot);
    }

    void PlayerStatsService::PublishPending(const char* message)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.pending = true;
        m_Snapshot.message = message ? message : "Working...";
    }

    void PlayerStatsService::PublishFailure(const char* message)
    {
        m_Pending.store(false, std::memory_order_release);
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.pending = false;
        m_Snapshot.lastSucceeded = false;
        m_Snapshot.message = message ? message : "Stat operation failed";
        m_Snapshot.revision = ++m_Revision;
    }
}
