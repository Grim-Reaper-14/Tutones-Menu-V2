#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>

namespace TutonesV2::Features::Parity
{
    enum class InstantResupplyTarget : std::size_t
    {
        Slot0,
        Slot1,
        Slot2,
        Slot3,
        Slot4,
        Bunker,
        AcidLab,
    };

    struct BunkerProfile final
    {
        int productValue{5000};
        float nearSaleMultiplier{1.0f};
        float farSaleMultiplier{1.5f};
        float highDemandBonus{2.5f};
        float highDemandMaxBonus{20.0f};
        int manufacturingProductionMs{600000};
        int researchProductionMs{300000};
    };

    struct ParityGlobalsSnapshot final
    {
        bool pending{};
        bool haveResult{};
        bool lastSucceeded{};
        bool bunkerFastProduction{};
        std::string message{"Ready"};
    };

    class ParityGlobalsService final
    {
    public:
        static ParityGlobalsService& Get() noexcept;

        [[nodiscard]] ParityGlobalsSnapshot Snapshot() const;

        bool QueueInstantResupply(InstantResupplyTarget target) noexcept;
        bool QueueGoodBehaviorBonus() noexcept;

        bool QueueBunkerFastProduction(bool enabled) noexcept;
        bool QueueBunkerProfile(BunkerProfile profile) noexcept;

        bool QueueSpecialCargoSourcing(
            int amount,
            int cargoType,
            int specialItem,
            bool specialAvailable) noexcept;
        bool QueueSpecialCargoCooldowns(int buyMilliseconds, int sellMilliseconds) noexcept;
        bool QueueSpecialCargoCratePrice(int tierIndex, int value) noexcept;
        bool QueueSpecialCargoUniqueItem(int uniqueItemValue) noexcept;

    private:
        ParityGlobalsService() = default;

        template<typename Callback>
        bool Queue(std::string pendingMessage, Callback&& callback)
        {
            bool expected = false;
            if (!m_Pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return false;

            SetPending(std::move(pendingMessage));
            return QueueImpl(std::forward<Callback>(callback));
        }

        template<typename Callback>
        bool QueueImpl(Callback&& callback);

        [[nodiscard]] std::int64_t** RequireGlobals() noexcept;
        void SetPending(std::string message);
        void Finish(bool success, std::string message);

        std::atomic_bool m_Pending{};
        std::atomic_bool m_BunkerFastProduction{};
        int m_SavedManufacturingMs{};
        mutable std::mutex m_Mutex;
        bool m_HaveResult{};
        bool m_LastSucceeded{};
        std::string m_Message{"Ready"};
    };
}
