#pragma once

#include "../features/parity/ParityGlobalsService.hpp"
#include "../features/business/NightclubRuntime.hpp"
#include "../features/business/MotorcycleClubRuntime.hpp"
#include "../features/business/VehicleCargoTuningRuntime.hpp"
#include "../features/business/AcidLabProductionRuntime.hpp"
#include "../features/business/BailOfficeRuntime.hpp"
#include "../features/business/MoneyFrontsRuntime.hpp"
#include "../features/business/GarmentFactoryRuntime.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace TutonesV2::UI
{
    inline void RenderBusinessParityPanel() noexcept
    {
        auto& parity = Features::Parity::ParityGlobalsService::Get();
        const auto parityState = parity.Snapshot();

        if (!ImGui::BeginTabBar("##v1_business_tabs", ImGuiTabBarFlags_FittingPolicyScroll))
            return;

        if (ImGui::BeginTabItem("Dashboard"))
        {
            ImGui::SeparatorText("Instant Resupply");
            constexpr std::array<const char*, 7> labels{{
                "MC Slot 0", "MC Slot 1", "MC Slot 2", "MC Slot 3", "MC Slot 4", "Bunker", "Acid Lab"
            }};

            ImGui::BeginDisabled(parityState.pending);
            for (std::size_t i = 0; i < labels.size(); ++i)
            {
                if (i != 0 && (i % 3) != 0)
                    ImGui::SameLine();
                if (ImGui::Button(labels[i], ImVec2(i < 5 ? 118.0f : 140.0f, 28.0f)))
                {
                    static_cast<void>(parity.QueueInstantResupply(
                        static_cast<Features::Parity::InstantResupplyTarget>(i)));
                }
            }
            ImGui::EndDisabled();

            ImGui::TextDisabled("V1 global status: %s", parityState.message.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Bunker"))
        {
            static Features::Parity::BunkerProfile profile{};
            bool fastProduction = parityState.bunkerFastProduction;

            ImGui::BeginDisabled(parityState.pending);
            if (ImGui::Checkbox("Fast Production (1 second)", &fastProduction))
                static_cast<void>(parity.QueueBunkerFastProduction(fastProduction));

            ImGui::InputInt("Product Value", &profile.productValue, 100, 1000);
            ImGui::InputFloat("Near Sale Multiplier", &profile.nearSaleMultiplier, 0.1f, 0.5f, "%.2f");
            ImGui::InputFloat("Far Sale Multiplier", &profile.farSaleMultiplier, 0.1f, 0.5f, "%.2f");
            ImGui::InputFloat("High Demand Bonus", &profile.highDemandBonus, 0.1f, 0.5f, "%.2f");
            ImGui::InputFloat("High Demand Max Bonus", &profile.highDemandMaxBonus, 0.5f, 1.0f, "%.2f");
            ImGui::InputInt("Manufacturing Cycle (ms)", &profile.manufacturingProductionMs, 1000, 10000);
            ImGui::InputInt("Research Cycle (ms)", &profile.researchProductionMs, 1000, 10000);

            if (ImGui::Button("Apply Bunker Profile", ImVec2(-1.0f, 30.0f)))
                static_cast<void>(parity.QueueBunkerProfile(profile));
            if (ImGui::Button("Bunker Instant Sell", ImVec2(-1.0f, 28.0f)))
                static_cast<void>(parity.QueueBunkerInstantSell());
            ImGui::EndDisabled();

            ImGui::TextDisabled("%s", parityState.message.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Nightclub"))
        {
            using namespace Game::Business;
            auto& runtime = NightclubRuntime::Get();
            const auto state = runtime.Snapshot();
            static NightclubProfile profile = NightclubData::DefaultProfile();
            static int selectedGood{};
            static int popularity{1000};

            selectedGood = std::clamp(selectedGood, 0, static_cast<int>(NightclubData::GoodNames.size()) - 1);
            ImGui::Combo("Warehouse Good", &selectedGood, NightclubData::GoodNames.data(), static_cast<int>(NightclubData::GoodNames.size()));
            ImGui::InputInt("Stock Value", &profile.stockValues[static_cast<std::size_t>(selectedGood)], 100, 1000);
            ImGui::InputInt("Special Order Value", &profile.specialOrderStockValues[static_cast<std::size_t>(selectedGood)], 100, 1000);
            ImGui::InputInt("Max Units", &profile.maxUnits[static_cast<std::size_t>(selectedGood)], 1, 10);
            ImGui::InputInt("Production Time (ms)", &profile.productionTimes[static_cast<std::size_t>(selectedGood)], 1000, 10000);

            ImGui::BeginDisabled(state.actionPending);
            if (ImGui::Button("Apply Selected Good"))
                static_cast<void>(runtime.QueueApplyGood(static_cast<std::size_t>(selectedGood), profile));

            bool instant = state.instantProductionEnabled;
            if (ImGui::Checkbox("Instant Nightclub Production", &instant))
                static_cast<void>(runtime.QueueSetInstantProduction(instant));

            popularity = std::clamp(popularity, 0, NightclubData::MaximumPopularity);
            ImGui::InputInt("Popularity", &popularity, 10, 100);
            if (ImGui::Button("Set Popularity"))
                static_cast<void>(runtime.QueueSetPopularity(popularity));

            if (ImGui::Button("Apply Full Nightclub Profile", ImVec2(-1.0f, 28.0f)))
                static_cast<void>(runtime.QueueApplyProfile(profile));
            ImGui::EndDisabled();

            ImGui::TextDisabled("%s", state.message.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Special Cargo"))
        {
            static int amount{3};
            static int cargoType{-1};
            static int specialItem{};
            static bool specialAvailable{};
            static int buyCooldown{};
            static int sellCooldown{};
            static int crateTier{};
            static int cratePrice{};
            static int uniqueItem{2};

            ImGui::BeginDisabled(parityState.pending);
            ImGui::InputInt("Source Amount", &amount, 1, 10);
            amount = std::clamp(amount, 1, 111);
            ImGui::InputInt("Cargo Type", &cargoType, 1, 1);
            cargoType = std::clamp(cargoType, -1, 10);
            ImGui::InputInt("Special Item", &specialItem, 1, 1);
            specialItem = std::clamp(specialItem, 0, 5);
            ImGui::Checkbox("Special Available", &specialAvailable);

            if (ImGui::Button("Apply Sourcing"))
                static_cast<void>(parity.QueueSpecialCargoSourcing(amount, cargoType, specialItem, specialAvailable));

            ImGui::InputInt("Buy Cooldown (ms)", &buyCooldown, 1000, 10000);
            ImGui::InputInt("Sell Cooldown (ms)", &sellCooldown, 1000, 10000);
            buyCooldown = std::max(0, buyCooldown);
            sellCooldown = std::max(0, sellCooldown);
            if (ImGui::Button("Apply Cooldowns"))
                static_cast<void>(parity.QueueSpecialCargoCooldowns(buyCooldown, sellCooldown));

            ImGui::InputInt("Crate Tier", &crateTier, 1, 1);
            crateTier = std::clamp(crateTier, 0, 20);
            ImGui::InputInt("Crate Price", &cratePrice, 1000, 10000);
            cratePrice = std::max(0, cratePrice);
            if (ImGui::Button("Apply Crate Price"))
                static_cast<void>(parity.QueueSpecialCargoCratePrice(crateTier, cratePrice));

            constexpr std::array<int, 6> uniqueItems{{2, 4, 6, 7, 8, 9}};
            constexpr std::array<const char*, 6> uniqueLabels{{
                "Ornamental Egg", "Golden Minigun", "Large Diamond", "Rare Hide", "Film Reel", "Pocket Watch"
            }};
            int uniqueIndex{};
            for (std::size_t i = 0; i < uniqueItems.size(); ++i)
                if (uniqueItems[i] == uniqueItem) uniqueIndex = static_cast<int>(i);
            if (ImGui::Combo("Unique Cargo", &uniqueIndex, uniqueLabels.data(), static_cast<int>(uniqueLabels.size())))
                uniqueItem = uniqueItems[static_cast<std::size_t>(uniqueIndex)];
            if (ImGui::Button("Enable Unique Cargo"))
                static_cast<void>(parity.QueueSpecialCargoUniqueItem(uniqueItem));

            if (ImGui::Button("Instant Buy"))
                static_cast<void>(parity.QueueSpecialCargoInstantBuy());
            ImGui::SameLine();
            if (ImGui::Button("Instant Sell"))
                static_cast<void>(parity.QueueSpecialCargoInstantSell());
            ImGui::EndDisabled();

            ImGui::TextDisabled("%s", parityState.message.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Motorcycle Club"))
        {
            using namespace Game::Business;
            auto& runtime = MotorcycleClubRuntime::Get();
            const auto state = runtime.Snapshot();
            static MotorcycleClubProfile profile = MotorcycleClubData::DefaultProfile();
            static int selectedBusiness{};

            selectedBusiness = std::clamp(selectedBusiness, 0, static_cast<int>(MotorcycleClubData::BusinessNames.size()) - 1);
            ImGui::Combo("Business", &selectedBusiness, MotorcycleClubData::BusinessNames.data(), static_cast<int>(MotorcycleClubData::BusinessNames.size()));
            ImGui::InputInt("Stock Value", &profile.stockValues[static_cast<std::size_t>(selectedBusiness)], 100, 1000);
            ImGui::InputInt("Max Capacity", &profile.maxCapacities[static_cast<std::size_t>(selectedBusiness)], 1, 10);
            ImGui::InputFloat("Near Sale", &profile.nearSaleMultiplier, 0.1f, 0.5f, "%.2f");
            ImGui::InputFloat("Far Sale", &profile.farSaleMultiplier, 0.1f, 0.5f, "%.2f");

            ImGui::BeginDisabled(state.actionPending);
            if (ImGui::Button("Apply Selected Business"))
                static_cast<void>(runtime.QueueApplyBusiness(static_cast<std::size_t>(selectedBusiness), profile));
            if (ImGui::Button("Apply Sale Multipliers"))
                static_cast<void>(runtime.QueueApplySaleMultipliers(profile.nearSaleMultiplier, profile.farSaleMultiplier));
            if (ImGui::Button("Apply Full MC Profile", ImVec2(-1.0f, 28.0f)))
                static_cast<void>(runtime.QueueApplyProfile(profile));
            ImGui::EndDisabled();

            ImGui::TextDisabled("%s", state.message.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Acid Lab"))
        {
            using Game::Business::AcidLabProductionRuntime;
            auto& runtime = AcidLabProductionRuntime::Get();
            const auto state = runtime.Snapshot();

            ImGui::BeginDisabled(state.actionPending);
            if (ImGui::Button("Instant Fill Acid Lab Stock", ImVec2(-1.0f, 30.0f)))
                static_cast<void>(runtime.QueueInstantFinish());
            ImGui::EndDisabled();

            if (state.stockUnits >= 0)
                ImGui::Text("Stock: %d / 160", state.stockUnits);
            ImGui::Text("Persistent stat: %s", state.persistentStockVerified ? "VERIFIED" : "WAITING");
            ImGui::Text("Live cache: %s", state.liveCacheUpdated ? "UPDATED" : "UNCHANGED");
            ImGui::TextDisabled("%s", state.message.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Vehicle Cargo"))
        {
            using namespace Game::Business;
            auto& runtime = VehicleCargoTuningRuntime::Get();
            const auto state = runtime.Snapshot();
            static VehicleCargoTuningProfile profile{};

            if (state.readable)
                profile = state.values;

            ImGui::InputInt("Steal Cooldown (ms)", &profile.stealCooldownMs, 1000, 10000);
            ImGui::InputInt("Sell Cooldown 1", &profile.sellCooldown1Ms, 1000, 10000);
            ImGui::InputInt("Sell Cooldown 2", &profile.sellCooldown2Ms, 1000, 10000);
            ImGui::InputInt("Sell Cooldown 3", &profile.sellCooldown3Ms, 1000, 10000);
            ImGui::InputInt("Sell Cooldown 4", &profile.sellCooldown4Ms, 1000, 10000);
            ImGui::InputInt("Top Range Price", &profile.topRangeSellPrice, 1000, 10000);
            ImGui::InputInt("Mid Range Price", &profile.midRangeSellPrice, 1000, 10000);
            ImGui::InputInt("Standard Range Price", &profile.standardRangeSellPrice, 1000, 10000);

            ImGui::BeginDisabled(state.pending);
            if (ImGui::Button("Refresh Vehicle Cargo"))
                static_cast<void>(runtime.QueueRefresh());
            ImGui::SameLine();
            if (ImGui::Button("Apply Vehicle Cargo"))
                static_cast<void>(runtime.QueueApplyProfile(profile));
            ImGui::EndDisabled();

            ImGui::TextDisabled("%s", state.message.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Bail Office"))
        {
            using namespace Game::Business;
            auto& runtime = BailOfficeRuntime::Get();
            const auto state = runtime.Snapshot();

            ImGui::BeginDisabled(state.pending);
            if (ImGui::Button("Refresh Bail Office", ImVec2(-1.0f, 28.0f)))
                static_cast<void>(runtime.QueueRefresh());
            ImGui::EndDisabled();

            ImGui::Text("App: %s | Globals: %s", state.appRunning ? "RUNNING" : "IDLE", state.globalsReady ? "READY" : "WAITING");
            for (std::size_t i = 0; i < state.standardTargets.size(); ++i)
            {
                const auto& target = state.standardTargets[i];
                ImGui::Text(
                    "Target %zu: %s | $%d | %s",
                    i + 1,
                    target.readable ? BailOfficeEnhanced173::TargetName(target.target) : "unavailable",
                    target.reward,
                    target.completed ? "DONE" : "OPEN");
            }
            if (state.mostWantedReadable)
                ImGui::Text("Most Wanted rotation: %d", state.mostWantedRotation);
            ImGui::TextDisabled("%s", state.message.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Money Fronts"))
        {
            using namespace Game::Business;
            auto& runtime = MoneyFrontsRuntime::Get();
            const auto state = runtime.Snapshot();
            static int heat{0};
            heat = std::clamp(heat, MoneyFrontsEnhanced173::MinimumHeat, MoneyFrontsEnhanced173::MaximumHeat);

            ImGui::BeginDisabled(state.pending);
            if (ImGui::Button("Refresh Money Fronts", ImVec2(-1.0f, 28.0f)))
                static_cast<void>(runtime.QueueRefresh());
            ImGui::InputInt("Heat", &heat, 1, 10);
            heat = std::clamp(heat, MoneyFrontsEnhanced173::MinimumHeat, MoneyFrontsEnhanced173::MaximumHeat);

            for (int i = 0; i < MoneyFrontsEnhanced173::FrontCount; ++i)
            {
                if (ImGui::Button(MoneyFrontsEnhanced173::FrontNames[static_cast<std::size_t>(i)]))
                    static_cast<void>(runtime.QueueSetHeat(i, heat));
                if (state.heat[static_cast<std::size_t>(i)] >= 0)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("current %d", state.heat[static_cast<std::size_t>(i)]);
                }
            }

            if (ImGui::Button("Set All Heat"))
                static_cast<void>(runtime.QueueSetAllHeat(heat));
            if (ImGui::Button("Collect Car Wash Safe", ImVec2(-1.0f, 28.0f)))
                static_cast<void>(runtime.QueueCollectCarWashSafe());
            ImGui::EndDisabled();

            ImGui::Text("Car Wash safe: $%d", state.carWashSafeCash);
            ImGui::TextDisabled("%s", state.message.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Garment Factory"))
        {
            using namespace Game::Business;
            auto& runtime = GarmentFactoryRuntime::Get();
            const auto state = runtime.Snapshot();
            static int file{-1};

            file = std::clamp(file, -1, GarmentFactoryEnhanced173::FileCount - 1);

            ImGui::BeginDisabled(state.pending);
            if (ImGui::Button("Refresh Garment Factory", ImVec2(-1.0f, 28.0f)))
                static_cast<void>(runtime.QueueRefresh());

            const char* filePreview = GarmentFactoryEnhanced173::FibFileName(file);
            if (ImGui::BeginCombo("Active FIB File", filePreview))
            {
                for (int value = -1; value < GarmentFactoryEnhanced173::FileCount; ++value)
                {
                    const bool selected = file == value;
                    if (ImGui::Selectable(GarmentFactoryEnhanced173::FibFileName(value), selected))
                        file = value;
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Set Active File"))
                static_cast<void>(runtime.QueueSetActiveFile(file));
            if (ImGui::Button("Complete Preps"))
                static_cast<void>(runtime.QueueSetPrepsComplete(true));
            ImGui::SameLine();
            if (ImGui::Button("Reset Preps"))
                static_cast<void>(runtime.QueueSetPrepsComplete(false));
            if (ImGui::Button("Unbrick Computer"))
                static_cast<void>(runtime.QueueUnbrickComputer());
            if (ImGui::Button("Collect Factory Safe", ImVec2(-1.0f, 28.0f)))
                static_cast<void>(runtime.QueueCollectSafe());
            ImGui::EndDisabled();

            ImGui::Text("Current file: %s", GarmentFactoryEnhanced173::FibFileName(state.activeRobberyStat));
            ImGui::Text("Preps: %s | Safe: $%d", state.prepsComplete ? "COMPLETE" : "NOT COMPLETE", state.safeCash);
            ImGui::TextDisabled("%s", state.message.c_str());
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
