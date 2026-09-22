#include "ParityGlobalsService.hpp"

#include "../../core/Logger.hpp"
#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativePointers.hpp"
#include "../../game/script/ScriptGlobal.hpp"
#include "../../game/script/ScriptLocal.hpp"
#include "../../game/script/ScriptRuntime.hpp"
#include "../../game/Stats.hpp"
#include "../../game/native/NativeInvoker.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace TutonesV2::Features::Parity
{
    namespace
    {
        constexpr std::size_t InstantResupplyGlobal = 1673820;
        constexpr std::array<std::size_t, 7> InstantResupplyOffsets{{1, 2, 3, 4, 5, 6, 7}};

        constexpr std::size_t TunablesGlobal = 262145;

        constexpr std::size_t BunkerProductValueOffset = 21347;
        constexpr std::size_t BunkerNearSaleMultiplierOffset = 21319;
        constexpr std::size_t BunkerFarSaleMultiplierOffset = 21320;
        constexpr std::size_t BunkerHighDemandBonusOffset = 21232;
        constexpr std::size_t BunkerHighDemandMaxBonusOffset = 21233;
        constexpr std::size_t BunkerManufacturingProductionOffset = 21342;
        constexpr std::size_t BunkerResearchProductionOffset = 21358;
        constexpr int FastManufacturingProductionMs = 1000;

        constexpr std::size_t SpecialCargoSourcingBaseGlobal = 1882762;
        constexpr std::size_t SpecialCargoSourcingAmountOffset = 13;
        constexpr std::size_t SpecialCargoSpecialItemOffset = 14;
        constexpr std::size_t SpecialCargoSpecialAvailableOffset = 15;
        constexpr std::size_t SpecialCargoTypeOffset = 16;
        constexpr std::size_t SpecialCargoBuyCooldownOffset = 15592;
        constexpr std::size_t SpecialCargoSellCooldownOffset = 15593;
        constexpr std::size_t SpecialCargoFirstCratePriceOffset = 15825;
        constexpr int SpecialCargoCratePriceTierCount = 21;
        constexpr std::size_t UniqueSpecialAvailableGlobal = 1951074;
        constexpr std::size_t UniqueSpecialItemGlobal = 1950921;

        constexpr std::uint32_t GunrunningHash = Game::Stats::Detail::Joaat("gb_gunrunning");
        constexpr std::size_t GunrunningInstantSellLocal = 1275 + 774;

        constexpr std::uint32_t ContrabandBuyHash = Game::Stats::Detail::Joaat("gb_contraband_buy");
        constexpr std::uint32_t ContrabandSellHash = Game::Stats::Detail::Joaat("gb_contraband_sell");

        constexpr const char* AcidLabSetupStat = "MPX_FACTORYSETUP6";
        constexpr const char* AcidLabStockStat = "MPX_PRODTOTALFORFACTORY6";
        constexpr int AcidLabMaxStock = 160;
        constexpr std::size_t AcidPlayerFreemodeGlobal = 1845347;
        constexpr std::size_t AcidPlayerFreemodeStride = 884;
        constexpr std::size_t AcidPropertyDataOffset = 260;
        constexpr std::size_t AcidFactoryArrayOffset = 205;
        constexpr std::size_t AcidFactoryEntryStride = 13;
        constexpr std::size_t AcidFactoryIndex = 6;
        constexpr std::size_t AcidProductOffset = 1;
        constexpr int AcidFactoryArrayCount = 7;
        constexpr int AcidFactoryType = 32;

        constexpr std::size_t LuckyWheelTunablesGlobal = 262145;
        constexpr std::size_t LuckyWheelMaxSpinsOffset = 26855;
        constexpr std::size_t LuckyWheelAdditionalSpinsOffset = 26856;
        constexpr std::size_t LuckyWheelGtaPlusMaxSpinsOffset = 37458;
        constexpr std::uint32_t LuckyWheelScriptHash = Game::Stats::Detail::Joaat("casino_lucky_wheel");
        constexpr std::size_t LuckyWheelPlayerArrayBase = 150;
        constexpr std::size_t LuckyWheelPlayerArrayHeader = 1;
        constexpr std::size_t LuckyWheelPlayerStride = 5;

        constexpr std::size_t GoodBehaviorTriggerGlobal = 2697090;
        constexpr std::size_t GoodBehaviorRewardGlobal = 2697091;
        constexpr int GoodBehaviorRewardAmount = 2000;

        bool IsKnownUniqueSpecialItem(int value) noexcept
        {
            switch (value)
            {
            case 2:
            case 4:
            case 6:
            case 7:
            case 8:
            case 9:
                return true;
            default:
                return false;
            }
        }
    }

    ParityGlobalsService& ParityGlobalsService::Get() noexcept
    {
        static ParityGlobalsService instance;
        return instance;
    }

    ParityGlobalsSnapshot ParityGlobalsService::Snapshot() const
    {
        ParityGlobalsSnapshot snapshot{};
        snapshot.pending = m_Pending.load(std::memory_order_acquire);
        snapshot.bunkerFastProduction = m_BunkerFastProduction.load(std::memory_order_acquire);

        std::scoped_lock lock(m_Mutex);
        snapshot.haveResult = m_HaveResult;
        snapshot.lastSucceeded = m_LastSucceeded;
        snapshot.message = m_Message;
        return snapshot;
    }

    template<typename Callback>
    bool ParityGlobalsService::QueueImpl(Callback&& callback)
    {
        if (Game::GameRuntime::Get().Enqueue(std::forward<Callback>(callback)))
            return true;

        Finish(false, "Game-thread queue unavailable");
        return false;
    }

    std::int64_t** ParityGlobalsService::RequireGlobals() noexcept
    {
        bool* sessionStarted = Game::Native::NativePointers::Get().IsSessionStarted();
        if (!sessionStarted || !*sessionStarted)
        {
            Finish(false, "Join GTA Online before using this V1 global tool");
            return nullptr;
        }

        auto* pages = Game::Native::NativePointers::Get().ScriptGlobals();
        if (!pages)
        {
            Finish(false, "Enhanced script globals are unavailable");
            return nullptr;
        }

        return pages;
    }

    bool ParityGlobalsService::QueueInstantResupply(InstantResupplyTarget target) noexcept
    {
        const auto index = static_cast<std::size_t>(target);
        if (index >= InstantResupplyOffsets.size())
            return false;

        const auto offset = InstantResupplyOffsets[index];
        return Queue("Instant Resupply queued", [this, offset] {
            auto* pages = RequireGlobals();
            if (!pages)
                return;

            int* value = Game::Script::ScriptGlobal(InstantResupplyGlobal).At(offset).As<int>(pages);
            if (!value)
                return Finish(false, "Instant Resupply global is unavailable");

            *value = 1;
            const bool success = *value == 1;
            if (success)
            {
                Core::Logger::Get().Info(
                    "parity.business",
                    std::string("Instant Resupply applied at Global_1673820 + ")
                        + std::to_string(offset));
            }
            Finish(success, success ? "Instant Resupply applied" : "Instant Resupply failed verification");
        });
    }

    bool ParityGlobalsService::QueueGoodBehaviorBonus() noexcept
    {
        return Queue("Good Behavior Bonus queued", [this] {
            auto* pages = RequireGlobals();
            if (!pages)
                return;

            int* reward = Game::Script::ScriptGlobal(GoodBehaviorRewardGlobal).As<int>(pages);
            int* trigger = Game::Script::ScriptGlobal(GoodBehaviorTriggerGlobal).As<int>(pages);
            if (!reward || !trigger)
                return Finish(false, "Good Behavior Bonus globals are unavailable");

            *reward = GoodBehaviorRewardAmount;
            std::atomic_thread_fence(std::memory_order_seq_cst);
            *trigger = 1;

            const bool success = *reward == GoodBehaviorRewardAmount && *trigger == 1;
            Finish(
                success,
                success
                    ? "Good Behavior Bonus triggered for $2,000"
                    : "Good Behavior Bonus failed verification");
        });
    }

    bool ParityGlobalsService::QueueBunkerFastProduction(bool enabled) noexcept
    {
        return Queue(
            enabled ? "Bunker fast production queued" : "Bunker production restore queued",
            [this, enabled] {
                auto* pages = RequireGlobals();
                if (!pages)
                    return;

                int* manufacturing = Game::Script::ScriptGlobal(TunablesGlobal)
                    .At(BunkerManufacturingProductionOffset)
                    .As<int>(pages);
                if (!manufacturing)
                    return Finish(false, "Bunker manufacturing tunable is unavailable");

                const bool wasEnabled = m_BunkerFastProduction.load(std::memory_order_acquire);
                if (enabled)
                {
                    if (!wasEnabled)
                        m_SavedManufacturingMs = *manufacturing > 0 ? *manufacturing : 600000;

                    *manufacturing = FastManufacturingProductionMs;
                    const bool success = *manufacturing == FastManufacturingProductionMs;
                    if (success)
                        m_BunkerFastProduction.store(true, std::memory_order_release);
                    return Finish(
                        success,
                        success ? "Bunker fast production enabled" : "Bunker fast production failed verification");
                }

                const int restore = m_SavedManufacturingMs > 0 ? m_SavedManufacturingMs : 600000;
                *manufacturing = restore;
                const bool success = *manufacturing == restore;
                if (success)
                {
                    m_BunkerFastProduction.store(false, std::memory_order_release);
                    m_SavedManufacturingMs = 0;
                }
                Finish(success, success ? "Bunker normal production restored" : "Bunker restore failed verification");
            });
    }

    bool ParityGlobalsService::QueueBunkerProfile(BunkerProfile profile) noexcept
    {
        if (profile.productValue < 0
            || profile.nearSaleMultiplier < 0.0f
            || profile.farSaleMultiplier < 0.0f
            || profile.highDemandBonus < 0.0f
            || profile.highDemandMaxBonus < 0.0f
            || profile.manufacturingProductionMs < 0
            || profile.researchProductionMs < 0)
        {
            return false;
        }

        return Queue("Bunker tuning profile queued", [this, profile] {
            auto* pages = RequireGlobals();
            if (!pages)
                return;

            int* productValue = Game::Script::ScriptGlobal(TunablesGlobal).At(BunkerProductValueOffset).As<int>(pages);
            float* nearSale = Game::Script::ScriptGlobal(TunablesGlobal).At(BunkerNearSaleMultiplierOffset).As<float>(pages);
            float* farSale = Game::Script::ScriptGlobal(TunablesGlobal).At(BunkerFarSaleMultiplierOffset).As<float>(pages);
            float* demandBonus = Game::Script::ScriptGlobal(TunablesGlobal).At(BunkerHighDemandBonusOffset).As<float>(pages);
            float* maxDemandBonus = Game::Script::ScriptGlobal(TunablesGlobal).At(BunkerHighDemandMaxBonusOffset).As<float>(pages);
            int* manufacturing = Game::Script::ScriptGlobal(TunablesGlobal).At(BunkerManufacturingProductionOffset).As<int>(pages);
            int* research = Game::Script::ScriptGlobal(TunablesGlobal).At(BunkerResearchProductionOffset).As<int>(pages);

            if (!productValue || !nearSale || !farSale || !demandBonus || !maxDemandBonus || !manufacturing || !research)
                return Finish(false, "One or more Bunker tunables are unavailable");

            const bool fast = m_BunkerFastProduction.load(std::memory_order_acquire);
            const int manufacturingTarget = fast
                ? FastManufacturingProductionMs
                : profile.manufacturingProductionMs;
            if (fast)
                m_SavedManufacturingMs = profile.manufacturingProductionMs;

            *productValue = profile.productValue;
            *nearSale = profile.nearSaleMultiplier;
            *farSale = profile.farSaleMultiplier;
            *demandBonus = profile.highDemandBonus;
            *maxDemandBonus = profile.highDemandMaxBonus;
            *manufacturing = manufacturingTarget;
            *research = profile.researchProductionMs;

            const bool success = *productValue == profile.productValue
                && *nearSale == profile.nearSaleMultiplier
                && *farSale == profile.farSaleMultiplier
                && *demandBonus == profile.highDemandBonus
                && *maxDemandBonus == profile.highDemandMaxBonus
                && *manufacturing == manufacturingTarget
                && *research == profile.researchProductionMs;

            Finish(success, success ? "Bunker tuning profile applied" : "Bunker tuning profile failed verification");
        });
    }

    bool ParityGlobalsService::QueueSpecialCargoSourcing(
        int amount,
        int cargoType,
        int specialItem,
        bool specialAvailable) noexcept
    {
        if (amount < 1 || amount > 111
            || cargoType < -1 || cargoType > 10
            || specialItem < 0 || specialItem > 5)
        {
            return false;
        }

        return Queue("Special Cargo sourcing settings queued", [this, amount, cargoType, specialItem, specialAvailable] {
            auto* pages = RequireGlobals();
            if (!pages)
                return;

            int* amountGlobal = Game::Script::ScriptGlobal(SpecialCargoSourcingBaseGlobal)
                .At(SpecialCargoSourcingAmountOffset).As<int>(pages);
            int* typeGlobal = Game::Script::ScriptGlobal(SpecialCargoSourcingBaseGlobal)
                .At(SpecialCargoTypeOffset).As<int>(pages);
            int* specialGlobal = Game::Script::ScriptGlobal(SpecialCargoSourcingBaseGlobal)
                .At(SpecialCargoSpecialItemOffset).As<int>(pages);
            int* availableGlobal = Game::Script::ScriptGlobal(SpecialCargoSourcingBaseGlobal)
                .At(SpecialCargoSpecialAvailableOffset).As<int>(pages);

            if (!amountGlobal || !typeGlobal || !specialGlobal || !availableGlobal)
                return Finish(false, "Special Cargo sourcing globals are unavailable");

            *amountGlobal = amount;
            *typeGlobal = cargoType;
            *specialGlobal = specialItem;
            *availableGlobal = specialAvailable ? 1 : 0;

            const bool success = *amountGlobal == amount
                && *typeGlobal == cargoType
                && *specialGlobal == specialItem
                && *availableGlobal == (specialAvailable ? 1 : 0);

            Finish(success, success ? "Special Cargo sourcing settings applied" : "Special Cargo sourcing failed verification");
        });
    }

    bool ParityGlobalsService::QueueSpecialCargoCooldowns(
        int buyMilliseconds,
        int sellMilliseconds) noexcept
    {
        if (buyMilliseconds < 0 || sellMilliseconds < 0)
            return false;

        return Queue("Special Cargo cooldowns queued", [this, buyMilliseconds, sellMilliseconds] {
            auto* pages = RequireGlobals();
            if (!pages)
                return;

            int* buy = Game::Script::ScriptGlobal(TunablesGlobal)
                .At(SpecialCargoBuyCooldownOffset).As<int>(pages);
            int* sell = Game::Script::ScriptGlobal(TunablesGlobal)
                .At(SpecialCargoSellCooldownOffset).As<int>(pages);

            if (!buy || !sell)
                return Finish(false, "Special Cargo cooldown tunables are unavailable");

            *buy = buyMilliseconds;
            *sell = sellMilliseconds;
            Finish(
                *buy == buyMilliseconds && *sell == sellMilliseconds,
                *buy == buyMilliseconds && *sell == sellMilliseconds
                    ? "Special Cargo cooldowns applied"
                    : "Special Cargo cooldowns failed verification");
        });
    }

    bool ParityGlobalsService::QueueSpecialCargoCratePrice(int tierIndex, int value) noexcept
    {
        if (tierIndex < 0 || tierIndex >= SpecialCargoCratePriceTierCount || value < 0)
            return false;

        return Queue("Special Cargo crate price queued", [this, tierIndex, value] {
            auto* pages = RequireGlobals();
            if (!pages)
                return;

            int* price = Game::Script::ScriptGlobal(TunablesGlobal)
                .At(SpecialCargoFirstCratePriceOffset + static_cast<std::size_t>(tierIndex))
                .As<int>(pages);
            if (!price)
                return Finish(false, "Special Cargo crate price tunable is unavailable");

            *price = value;
            Finish(
                *price == value,
                *price == value
                    ? "Special Cargo crate price applied"
                    : "Special Cargo crate price failed verification");
        });
    }

    bool ParityGlobalsService::QueueSpecialCargoUniqueItem(int uniqueItemValue) noexcept
    {
        if (!IsKnownUniqueSpecialItem(uniqueItemValue))
            return false;

        return Queue("Unique Special Cargo item queued", [this, uniqueItemValue] {
            auto* pages = RequireGlobals();
            if (!pages)
                return;

            int* unique = Game::Script::ScriptGlobal(UniqueSpecialItemGlobal).As<int>(pages);
            int* available = Game::Script::ScriptGlobal(UniqueSpecialAvailableGlobal).As<int>(pages);
            if (!unique || !available)
                return Finish(false, "Unique Special Cargo globals are unavailable");

            *unique = uniqueItemValue;
            *available = 1;
            const bool success = *unique == uniqueItemValue && *available == 1;
            Finish(success, success ? "Unique Special Cargo item enabled" : "Unique Special Cargo failed verification");
        });
    }

    bool ParityGlobalsService::QueueBunkerInstantSell() noexcept
    {
        return Queue("Bunker instant-sell local queued", [this] {
            auto* sessionStarted = Game::Native::NativePointers::Get().IsSessionStarted();
            if (!sessionStarted || !*sessionStarted)
                return Finish(false, "Join GTA Online before using Bunker Instant Sell");

            auto& scripts = Game::Script::ScriptRuntime::Get();
            if (!scripts.IsReady())
                return Finish(false, "Shared V2 script runtime is unavailable");

            auto* thread = scripts.FindThread(GunrunningHash);
            if (!thread || !thread->stack)
                return Finish(false, "gb_gunrunning is not active");

            int* missionState = Game::Script::ScriptLocal(thread, GunrunningInstantSellLocal).As<int>();
            if (!missionState)
                return Finish(false, "gb_gunrunning instant-sell local is unavailable");

            *missionState = 0;
            Finish(
                *missionState == 0,
                *missionState == 0
                    ? "Bunker Instant Sell local applied"
                    : "Bunker Instant Sell failed verification");
        });
    }

    bool ParityGlobalsService::QueueSpecialCargoInstantBuy() noexcept
    {
        return Queue("Instant Special Cargo buy locals queued", [this] {
            auto* sessionStarted = Game::Native::NativePointers::Get().IsSessionStarted();
            if (!sessionStarted || !*sessionStarted)
                return Finish(false, "Join GTA Online before using Special Cargo Instant Buy");

            auto* thread = Game::Script::ScriptRuntime::Get().FindThread(ContrabandBuyHash);
            if (!thread || !thread->stack)
                return Finish(false, "gb_contraband_buy is not active");

            int* stage = Game::Script::ScriptLocal(thread, 634 + 5).As<int>();
            int* result = Game::Script::ScriptLocal(thread, 634 + 191).As<int>();
            int* state = Game::Script::ScriptLocal(thread, 634 + 192).As<int>();
            if (!stage || !result || !state)
                return Finish(false, "gb_contraband_buy locals are unavailable");

            *stage = 1;
            *result = 6;
            *state = 4;

            const bool success = *stage == 1 && *result == 6 && *state == 4;
            Finish(
                success,
                success ? "Instant Special Cargo buy applied" : "Instant Special Cargo buy failed verification");
        });
    }

    bool ParityGlobalsService::QueueSpecialCargoInstantSell() noexcept
    {
        return Queue("Instant Special Cargo sell local queued", [this] {
            auto* sessionStarted = Game::Native::NativePointers::Get().IsSessionStarted();
            if (!sessionStarted || !*sessionStarted)
                return Finish(false, "Join GTA Online before using Special Cargo Instant Sell");

            auto* thread = Game::Script::ScriptRuntime::Get().FindThread(ContrabandSellHash);
            if (!thread || !thread->stack)
                return Finish(false, "gb_contraband_sell is not active");

            int* state = Game::Script::ScriptLocal(thread, 576 + 1).As<int>();
            if (!state)
                return Finish(false, "gb_contraband_sell local 577 is unavailable");

            *state = 67230;
            Finish(
                *state == 67230,
                *state == 67230 ? "Instant Special Cargo sell applied" : "Instant Special Cargo sell failed verification");
        });
    }

    bool ParityGlobalsService::QueueAcidLabFullStock() noexcept
    {
        return Queue("Acid Lab full-stock action queued", [this] {
            auto* sessionStarted = Game::Native::NativePointers::Get().IsSessionStarted();
            if (!sessionStarted || !*sessionStarted)
                return Finish(false, "Join GTA Online before filling Acid Lab stock");

            const auto character = Game::Stats::GetCharIndex();
            if (!character)
                return Finish(false, "Active GTA Online character index is unavailable");

            const auto setup = Game::Stats::GetInt(AcidLabSetupStat, *character);
            if (!setup || *setup <= 0)
                return Finish(false, "Complete the Acid Lab setup before filling stock");

            if (!Game::Stats::SetInt(AcidLabStockStat, AcidLabMaxStock, *character))
                return Finish(false, "GTA rejected the Acid Lab stock stat write");

            const auto verified = Game::Stats::GetInt(AcidLabStockStat, *character);
            if (!verified || *verified != AcidLabMaxStock)
                return Finish(false, "Acid Lab stock failed persistent read-back verification");

            bool liveCacheUpdated = false;
            const auto player = Game::Native::NativeInvoker::Invoke<std::int32_t>(
                Game::Native::NativeId::PlayerId);
            auto* pages = Game::Native::NativePointers::Get().ScriptGlobals();
            if (player && *player >= 0 && *player < 32 && pages)
            {
                const std::size_t factoryArray =
                    AcidPlayerFreemodeGlobal + 1
                    + (static_cast<std::size_t>(*player) * AcidPlayerFreemodeStride)
                    + AcidPropertyDataOffset + AcidFactoryArrayOffset;
                const std::size_t entry =
                    factoryArray + 1 + (AcidFactoryIndex * AcidFactoryEntryStride);

                int* factoryCount = Game::Script::ScriptGlobal(factoryArray).As<int>(pages);
                int* factoryType = Game::Script::ScriptGlobal(entry).As<int>(pages);
                int* liveStock = Game::Script::ScriptGlobal(entry).At(AcidProductOffset).As<int>(pages);

                if (factoryCount && factoryType && liveStock
                    && *factoryCount == AcidFactoryArrayCount
                    && *factoryType == AcidFactoryType
                    && *liveStock >= 0
                    && *liveStock <= AcidLabMaxStock)
                {
                    *liveStock = AcidLabMaxStock;
                    liveCacheUpdated = *liveStock == AcidLabMaxStock;
                }
            }

            Finish(
                true,
                liveCacheUpdated
                    ? "Acid Lab stock filled to 160/160 and live cache updated"
                    : "Acid Lab stock filled to 160/160; re-enter Acid Lab if the interior display needs refresh");
        });
    }

    bool ParityGlobalsService::QueueLuckyWheelGlobals() noexcept
    {
        return Queue("Lucky Wheel globals queued", [this] {
            auto* pages = RequireGlobals();
            if (!pages)
                return;

            int* maxSpins = Game::Script::ScriptGlobal(LuckyWheelTunablesGlobal)
                .At(LuckyWheelMaxSpinsOffset).As<int>(pages);
            int* additionalSpins = Game::Script::ScriptGlobal(LuckyWheelTunablesGlobal)
                .At(LuckyWheelAdditionalSpinsOffset).As<int>(pages);
            int* gtaPlusMaxSpins = Game::Script::ScriptGlobal(LuckyWheelTunablesGlobal)
                .At(LuckyWheelGtaPlusMaxSpinsOffset).As<int>(pages);
            if (!maxSpins || !additionalSpins || !gtaPlusMaxSpins)
                return Finish(false, "Lucky Wheel globals are unavailable");

            *maxSpins = 1;
            *additionalSpins = 1;
            *gtaPlusMaxSpins = 2;

            const bool success =
                *maxSpins == 1
                && *additionalSpins == 1
                && *gtaPlusMaxSpins == 2;

            Finish(
                success,
                success ? "Lucky Wheel spin globals applied" : "Lucky Wheel globals failed verification");
        });
    }

    bool ParityGlobalsService::QueueLuckyWheelPrize(int prize) noexcept
    {
        if (prize < 0 || prize > 19)
            return false;

        return Queue("Lucky Wheel prize local queued", [this, prize] {
            auto* sessionStarted = Game::Native::NativePointers::Get().IsSessionStarted();
            if (!sessionStarted || !*sessionStarted)
                return Finish(false, "Join GTA Online before selecting a Lucky Wheel prize");

            auto* thread = Game::Script::ScriptRuntime::Get().FindThread(LuckyWheelScriptHash);
            if (!thread || !thread->stack)
                return Finish(false, "casino_lucky_wheel is not active; approach/use the wheel first");

            const auto player = Game::Native::NativeInvoker::Invoke<std::int32_t>(
                Game::Native::NativeId::PlayerId);
            if (!player || *player < 0)
                return Finish(false, "PLAYER_ID could not be resolved");

            const auto index =
                LuckyWheelPlayerArrayBase
                + LuckyWheelPlayerArrayHeader
                + static_cast<std::size_t>(*player) * LuckyWheelPlayerStride;

            int* prizeOutcome = Game::Script::ScriptLocal(thread, index).As<int>();
            if (!prizeOutcome)
                return Finish(false, "Lucky Wheel prize local is unavailable");

            auto* pages = Game::Native::NativePointers::Get().ScriptGlobals();
            if (!pages)
                return Finish(false, "Enhanced script globals are unavailable");

            int* additionalSpins = Game::Script::ScriptGlobal(LuckyWheelTunablesGlobal)
                .At(LuckyWheelAdditionalSpinsOffset).As<int>(pages);
            int* gtaPlusMaxSpins = Game::Script::ScriptGlobal(LuckyWheelTunablesGlobal)
                .At(LuckyWheelGtaPlusMaxSpinsOffset).As<int>(pages);
            if (!additionalSpins || !gtaPlusMaxSpins)
                return Finish(false, "Lucky Wheel spin globals are unavailable");

            *additionalSpins = 1;
            *gtaPlusMaxSpins = 2;
            *prizeOutcome = prize;

            const bool success =
                *additionalSpins == 1
                && *gtaPlusMaxSpins == 2
                && *prizeOutcome == prize;

            Finish(
                success,
                success ? "Selected Lucky Wheel prize applied" : "Lucky Wheel prize failed verification");
        });
    }

    void ParityGlobalsService::SetPending(std::string message)
    {
        std::scoped_lock lock(m_Mutex);
        m_HaveResult = false;
        m_LastSucceeded = false;
        m_Message = std::move(message);
    }

    void ParityGlobalsService::Finish(bool success, std::string message)
    {
        {
            std::scoped_lock lock(m_Mutex);
            m_HaveResult = true;
            m_LastSucceeded = success;
            m_Message = std::move(message);
        }
        m_Pending.store(false, std::memory_order_release);
    }

}
