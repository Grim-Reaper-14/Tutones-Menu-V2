#include "PlayerService.hpp"

#include "../../core/Logger.hpp"
#include "../../game/GameRuntime.hpp"
#include "../../game/native/NativeInvoker.hpp"
#include "../../game/native/NativeRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace TutonesV2::Features::Player
{
    namespace
    {
        using Game::Native::NativeId;
        using Game::Native::NativeInvoker;

        constexpr float InfiniteUnderwaterSeconds = 2147483647.0f;
        constexpr float FullOxygenPercentage = 100.0f;
        constexpr int MaxModelAttempts = 300;
        constexpr int StandOnVehiclesFlag = 274;
        constexpr int DisableActionModeFlag = 200;
        constexpr int EveryoneIgnoreResetFlag = 124;
        constexpr std::uint32_t ParachuteHash = 0xFBAB5776u;

        [[nodiscard]] bool NativeReady() noexcept
        {
            return Game::GameRuntime::Get().NativeReady();
        }

        [[nodiscard]] int PlayerPed() noexcept
        {
            const auto value = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerPedId);
            return value ? *value : 0;
        }

        [[nodiscard]] int PlayerId() noexcept
        {
            const auto value = NativeInvoker::Invoke<std::int32_t>(NativeId::PlayerId);
            return value ? *value : -1;
        }

        void RestoreStaticPlayerState() noexcept
        {
            const int ped = PlayerPed();
            const int player = PlayerId();

            if (ped)
            {
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityInvincible, ped, std::int32_t{0}, std::int32_t{0}));
                static_cast<void>(NativeInvoker::InvokeVoid(
                    NativeId::SetEntityProofs,
                    ped,
                    std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, std::int32_t{0},
                    std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, std::int32_t{0}));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityVisible, ped, std::int32_t{1}, std::int32_t{0}));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedCanRagdoll, ped, std::int32_t{1}));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedSuffersCriticalHits, ped, std::int32_t{1}));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedMaxTimeUnderwater, ped, -1.0f));
            }

            if (player >= 0)
            {
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPoliceIgnorePlayer, player, std::int32_t{0}));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEveryoneIgnorePlayer, player, std::int32_t{0}));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetRunSprintMultiplierForPlayer, player, 1.0f));
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetSwimMultiplierForPlayer, player, 1.0f));
            }

            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetMobilePhoneRadioState, std::int32_t{0}));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetMobileRadioEnabledDuringGameplay, std::int32_t{0}));
        }
    }

    PlayerService& PlayerService::Get() noexcept
    {
        static PlayerService instance;
        return instance;
    }

    bool PlayerService::Initialize() noexcept
    {
        m_LoopQueued.store(false, std::memory_order_release);
        m_GodMode.store(false, std::memory_order_release);
        m_Bulletproof.store(false, std::memory_order_release);
        m_Invisible.store(false, std::memory_order_release);
        m_DisableCriticalHits.store(false, std::memory_order_release);
        m_KeepPlayerClean.store(false, std::memory_order_release);
        m_AquaLungs.store(false, std::memory_order_release);
        m_InfiniteOxygen.store(false, std::memory_order_release);
        m_NeverWanted.store(false, std::memory_order_release);
        m_PoliceIgnore.store(false, std::memory_order_release);
        m_EveryoneIgnore.store(false, std::memory_order_release);
        m_SuperJump.store(false, std::memory_order_release);
        m_InfiniteStamina.store(false, std::memory_order_release);
        m_NoRagdoll.store(false, std::memory_order_release);
        m_StandOnVehicles.store(false, std::memory_order_release);
        m_DisableActionMode.store(false, std::memory_order_release);
        m_InfiniteParachutes.store(false, std::memory_order_release);
        m_MobileRadio.store(false, std::memory_order_release);
        m_RunMultiplier.store(1.0f, std::memory_order_release);
        m_SwimMultiplier.store(1.0f, std::memory_order_release);
        m_PendingModel.store(0, std::memory_order_release);
        m_ModelAttempts = 0;
        m_Ready.store(true, std::memory_order_release);
        Core::Logger::Get().Info("self", "Player Self service initialized");
        return true;
    }

    void PlayerService::Shutdown() noexcept
    {
        if (!m_Ready.exchange(false, std::memory_order_acq_rel))
            return;

        const auto pendingModel = m_PendingModel.exchange(0, std::memory_order_acq_rel);
        m_LoopQueued.store(false, std::memory_order_release);
        m_GodMode.store(false, std::memory_order_release);
        m_Bulletproof.store(false, std::memory_order_release);
        m_Invisible.store(false, std::memory_order_release);
        m_DisableCriticalHits.store(false, std::memory_order_release);
        m_KeepPlayerClean.store(false, std::memory_order_release);
        m_AquaLungs.store(false, std::memory_order_release);
        m_InfiniteOxygen.store(false, std::memory_order_release);
        m_NeverWanted.store(false, std::memory_order_release);
        m_PoliceIgnore.store(false, std::memory_order_release);
        m_EveryoneIgnore.store(false, std::memory_order_release);
        m_SuperJump.store(false, std::memory_order_release);
        m_InfiniteStamina.store(false, std::memory_order_release);
        m_NoRagdoll.store(false, std::memory_order_release);
        m_StandOnVehicles.store(false, std::memory_order_release);
        m_DisableActionMode.store(false, std::memory_order_release);
        m_InfiniteParachutes.store(false, std::memory_order_release);
        m_MobileRadio.store(false, std::memory_order_release);
        m_RunMultiplier.store(1.0f, std::memory_order_release);
        m_SwimMultiplier.store(1.0f, std::memory_order_release);

        auto cleanup = [pendingModel] {
            RestoreStaticPlayerState();
            if (pendingModel)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetModelAsNoLongerNeeded, pendingModel));
        };

        auto& registry = Game::Native::NativeRegistry::Get();
        if (registry.CanInvokeOnCurrentThread())
        {
            cleanup();
            return;
        }

        auto& runtime = Game::GameRuntime::Get();
        if (!runtime.NativeReady())
            return;

        const auto completed = std::make_shared<std::atomic_bool>(false);
        if (!runtime.Enqueue([cleanup, completed] {
                cleanup();
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

    bool PlayerService::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire);
    }

    bool PlayerService::EnsureLoop() noexcept
    {
        if (!IsReady() || !NativeReady() || !HasPersistentWork())
            return false;

        bool expected = false;
        if (!m_LoopQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return true;

        if (Game::GameRuntime::Get().Enqueue([this] { Tick(); }))
            return true;

        m_LoopQueued.store(false, std::memory_order_release);
        return false;
    }

    void PlayerService::Tick() noexcept
    {
        if (!IsReady())
        {
            m_LoopQueued.store(false, std::memory_order_release);
            return;
        }

        ApplyPersistent();
        ProcessPendingModel();

        if (!HasPersistentWork())
        {
            m_LoopQueued.store(false, std::memory_order_release);
            return;
        }

        if (!Game::GameRuntime::Get().Enqueue([this] { Tick(); }))
            m_LoopQueued.store(false, std::memory_order_release);
    }

    void PlayerService::ApplyPersistent() noexcept
    {
        const int ped = PlayerPed();
        const int player = PlayerId();
        if (!ped)
            return;

        if (m_GodMode.load(std::memory_order_acquire))
        {
            const auto dead = NativeInvoker::Invoke<std::int32_t>(NativeId::IsEntityDead, ped, std::int32_t{1});
            const bool alive = !dead || *dead == 0;
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetEntityInvincible, ped, std::int32_t{alive ? 1 : 0}, std::int32_t{0}));
        }

        if (m_Bulletproof.load(std::memory_order_acquire))
        {
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetEntityProofs,
                ped,
                std::int32_t{1}, std::int32_t{0}, std::int32_t{0}, std::int32_t{0},
                std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, std::int32_t{0}));
        }

        if (m_Invisible.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityVisible, ped, std::int32_t{0}, std::int32_t{0}));

        if (m_DisableCriticalHits.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedSuffersCriticalHits, ped, std::int32_t{0}));

        if (m_KeepPlayerClean.load(std::memory_order_acquire))
        {
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearPedBloodDamage, ped));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearPedWetness, ped));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearPedEnvDirt, ped));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ResetPedVisibleDamage, ped));
        }

        if (m_InfiniteOxygen.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedMaxTimeUnderwater, ped, InfiniteUnderwaterSeconds));
        else if (m_AquaLungs.load(std::memory_order_acquire) && player >= 0)
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPlayerUnderwaterTimeRemaining, player, FullOxygenPercentage));

        if (m_NoRagdoll.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedCanRagdoll, ped, std::int32_t{0}));

        if (m_StandOnVehicles.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedResetFlag, ped, StandOnVehiclesFlag, std::int32_t{1}));

        if (m_DisableActionMode.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedResetFlag, ped, DisableActionModeFlag, std::int32_t{1}));

        if (player < 0)
            return;

        if (m_NeverWanted.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearPlayerWantedLevel, player));

        if (m_PoliceIgnore.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPoliceIgnorePlayer, player, std::int32_t{1}));

        if (m_EveryoneIgnore.load(std::memory_order_acquire))
        {
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEveryoneIgnorePlayer, player, std::int32_t{1}));
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetBlockingOfNonTemporaryEventsForAmbientPedsThisFrame,
                std::int32_t{1}));
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetPedResetFlag, ped, EveryoneIgnoreResetFlag, std::int32_t{1}));
        }

        if (m_SuperJump.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetSuperJumpThisFrame, player));

        if (m_InfiniteStamina.load(std::memory_order_acquire))
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::RestorePlayerStamina, player, 1.0f));

        const float runMultiplier = m_RunMultiplier.load(std::memory_order_acquire);
        if (runMultiplier > 1.001f)
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetRunSprintMultiplierForPlayer, player, runMultiplier));

        const float swimMultiplier = m_SwimMultiplier.load(std::memory_order_acquire);
        if (swimMultiplier > 1.001f)
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetSwimMultiplierForPlayer, player, swimMultiplier));

        if (m_InfiniteParachutes.load(std::memory_order_acquire))
        {
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPlayerHasReserveParachute, player));
            const auto hasParachute = NativeInvoker::Invoke<std::int32_t>(
                NativeId::HasPedGotWeapon, ped, ParachuteHash, std::int32_t{0});
            if (!hasParachute || *hasParachute == 0)
            {
                static_cast<void>(NativeInvoker::InvokeVoid(
                    NativeId::GiveWeaponToPed,
                    ped,
                    ParachuteHash,
                    1,
                    std::int32_t{0},
                    std::int32_t{0}));
            }
        }

        if (m_MobileRadio.load(std::memory_order_acquire))
        {
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetMobilePhoneRadioState, std::int32_t{1}));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetMobileRadioEnabledDuringGameplay, std::int32_t{1}));
        }
    }

    void PlayerService::ProcessPendingModel() noexcept
    {
        const auto model = m_PendingModel.load(std::memory_order_acquire);
        if (!model)
            return;

        const auto loaded = NativeInvoker::Invoke<std::int32_t>(NativeId::HasModelLoaded, model);
        if (loaded && *loaded != 0)
        {
            const int player = PlayerId();
            if (player >= 0
                && NativeInvoker::InvokeVoid(NativeId::SetPlayerModel, player, model))
            {
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetModelAsNoLongerNeeded, model));
                m_PendingModel.store(0, std::memory_order_release);
                m_ModelAttempts = 0;

                const int ped = PlayerPed();
                if (ped)
                    static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedDefaultComponentVariation, ped));

                ApplyPersistent();
                return;
            }
        }

        if (++m_ModelAttempts >= MaxModelAttempts)
        {
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetModelAsNoLongerNeeded, model));
            m_PendingModel.store(0, std::memory_order_release);
            m_ModelAttempts = 0;
            Core::Logger::Get().Warn("self", "Ped model request timed out");
        }
    }

    bool PlayerService::HasPersistentWork() const noexcept
    {
        return m_GodMode.load(std::memory_order_acquire)
            || m_Bulletproof.load(std::memory_order_acquire)
            || m_Invisible.load(std::memory_order_acquire)
            || m_DisableCriticalHits.load(std::memory_order_acquire)
            || m_KeepPlayerClean.load(std::memory_order_acquire)
            || m_AquaLungs.load(std::memory_order_acquire)
            || m_InfiniteOxygen.load(std::memory_order_acquire)
            || m_NeverWanted.load(std::memory_order_acquire)
            || m_PoliceIgnore.load(std::memory_order_acquire)
            || m_EveryoneIgnore.load(std::memory_order_acquire)
            || m_SuperJump.load(std::memory_order_acquire)
            || m_InfiniteStamina.load(std::memory_order_acquire)
            || m_NoRagdoll.load(std::memory_order_acquire)
            || m_StandOnVehicles.load(std::memory_order_acquire)
            || m_DisableActionMode.load(std::memory_order_acquire)
            || m_InfiniteParachutes.load(std::memory_order_acquire)
            || m_MobileRadio.load(std::memory_order_acquire)
            || m_RunMultiplier.load(std::memory_order_acquire) > 1.001f
            || m_SwimMultiplier.load(std::memory_order_acquire) > 1.001f
            || m_PendingModel.load(std::memory_order_acquire) != 0;
    }

    bool PlayerService::SetGodMode(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const bool previous = m_GodMode.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return true;
        if (enabled)
        {
            if (EnsureLoop()) return true;
            m_GodMode.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (ped) static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityInvincible, ped, std::int32_t{0}, std::int32_t{0}));
        });
        if (!queued) m_GodMode.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetBulletproof(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const bool previous = m_Bulletproof.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return true;
        if (enabled)
        {
            if (EnsureLoop()) return true;
            m_Bulletproof.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (!ped) return;
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetEntityProofs,
                ped,
                std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, std::int32_t{0},
                std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, std::int32_t{0}));
        });
        if (!queued) m_Bulletproof.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetInvisible(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const bool previous = m_Invisible.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return true;
        if (enabled)
        {
            if (EnsureLoop()) return true;
            m_Invisible.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (ped) static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEntityVisible, ped, std::int32_t{1}, std::int32_t{0}));
        });
        if (!queued) m_Invisible.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetDisableCriticalHits(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const bool previous = m_DisableCriticalHits.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return true;
        if (enabled)
        {
            if (EnsureLoop()) return true;
            m_DisableCriticalHits.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (ped) static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedSuffersCriticalHits, ped, std::int32_t{1}));
        });
        if (!queued) m_DisableCriticalHits.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetKeepPlayerClean(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        m_KeepPlayerClean.store(enabled, std::memory_order_release);
        return !enabled || EnsureLoop();
    }

    bool PlayerService::SetAquaLungs(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        m_AquaLungs.store(enabled, std::memory_order_release);
        return !enabled || EnsureLoop();
    }

    bool PlayerService::SetInfiniteOxygen(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const bool previous = m_InfiniteOxygen.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return true;
        if (enabled)
        {
            if (EnsureLoop()) return true;
            m_InfiniteOxygen.store(previous, std::memory_order_release);
            return false;
        }

        const bool aquaLungs = m_AquaLungs.load(std::memory_order_acquire);
        const bool queued = Game::GameRuntime::Get().Enqueue([aquaLungs] {
            const int ped = PlayerPed();
            const int player = PlayerId();
            if (ped) static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedMaxTimeUnderwater, ped, -1.0f));
            if (aquaLungs && player >= 0)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPlayerUnderwaterTimeRemaining, player, FullOxygenPercentage));
        });
        if (!queued) m_InfiniteOxygen.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetNeverWanted(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        m_NeverWanted.store(enabled, std::memory_order_release);
        return !enabled || EnsureLoop();
    }

    bool PlayerService::SetPoliceIgnore(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const bool previous = m_PoliceIgnore.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return true;
        if (enabled)
        {
            if (EnsureLoop()) return true;
            m_PoliceIgnore.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            const int player = PlayerId();
            if (player >= 0)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPoliceIgnorePlayer, player, std::int32_t{0}));
        });
        if (!queued) m_PoliceIgnore.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetEveryoneIgnore(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const bool previous = m_EveryoneIgnore.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return true;
        if (enabled)
        {
            if (EnsureLoop()) return true;
            m_EveryoneIgnore.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            const int player = PlayerId();
            if (player >= 0)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetEveryoneIgnorePlayer, player, std::int32_t{0}));
        });
        if (!queued) m_EveryoneIgnore.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetSuperJump(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        m_SuperJump.store(enabled, std::memory_order_release);
        return !enabled || EnsureLoop();
    }

    bool PlayerService::SetInfiniteStamina(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        m_InfiniteStamina.store(enabled, std::memory_order_release);
        return !enabled || EnsureLoop();
    }

    bool PlayerService::SetNoRagdoll(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const bool previous = m_NoRagdoll.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return true;
        if (enabled)
        {
            if (EnsureLoop()) return true;
            m_NoRagdoll.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (ped) static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedCanRagdoll, ped, std::int32_t{1}));
        });
        if (!queued) m_NoRagdoll.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetStandOnVehicles(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        m_StandOnVehicles.store(enabled, std::memory_order_release);
        return !enabled || EnsureLoop();
    }

    bool PlayerService::SetDisableActionMode(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        m_DisableActionMode.store(enabled, std::memory_order_release);
        return !enabled || EnsureLoop();
    }

    bool PlayerService::SetInfiniteParachutes(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        m_InfiniteParachutes.store(enabled, std::memory_order_release);
        return !enabled || EnsureLoop();
    }

    bool PlayerService::SetMobileRadio(bool enabled) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const bool previous = m_MobileRadio.exchange(enabled, std::memory_order_acq_rel);
        if (previous == enabled) return true;
        if (enabled)
        {
            if (EnsureLoop()) return true;
            m_MobileRadio.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetMobilePhoneRadioState, std::int32_t{0}));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetMobileRadioEnabledDuringGameplay, std::int32_t{0}));
        });
        if (!queued) m_MobileRadio.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetRunMultiplier(float multiplier) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        multiplier = std::clamp(multiplier, 1.0f, 1.49f);
        const float previous = m_RunMultiplier.exchange(multiplier, std::memory_order_acq_rel);
        if (multiplier > 1.001f)
        {
            if (EnsureLoop()) return true;
            m_RunMultiplier.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            const int player = PlayerId();
            if (player >= 0)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetRunSprintMultiplierForPlayer, player, 1.0f));
        });
        if (!queued) m_RunMultiplier.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::SetSwimMultiplier(float multiplier) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        multiplier = std::clamp(multiplier, 1.0f, 1.49f);
        const float previous = m_SwimMultiplier.exchange(multiplier, std::memory_order_acq_rel);
        if (multiplier > 1.001f)
        {
            if (EnsureLoop()) return true;
            m_SwimMultiplier.store(previous, std::memory_order_release);
            return false;
        }
        const bool queued = Game::GameRuntime::Get().Enqueue([] {
            const int player = PlayerId();
            if (player >= 0)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetSwimMultiplierForPlayer, player, 1.0f));
        });
        if (!queued) m_SwimMultiplier.store(previous, std::memory_order_release);
        return queued;
    }

    bool PlayerService::QueueSetHealth(int health) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const int requested = std::max(0, health);
        return Game::GameRuntime::Get().Enqueue([requested] {
            const int ped = PlayerPed();
            if (ped)
                static_cast<void>(NativeInvoker::InvokeVoid(
                    NativeId::SetEntityHealth, ped, requested, 0, std::uint32_t{0}));
        });
    }

    bool PlayerService::QueueHeal() noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        return Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (!ped) return;
            const auto maxHealth = NativeInvoker::Invoke<std::int32_t>(NativeId::GetEntityMaxHealth, ped);
            if (maxHealth)
                static_cast<void>(NativeInvoker::InvokeVoid(
                    NativeId::SetEntityHealth, ped, *maxHealth, 0, std::uint32_t{0}));
        });
    }

    bool PlayerService::QueueSetArmor(int armor) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const int requested = std::clamp(armor, 0, 100);
        return Game::GameRuntime::Get().Enqueue([requested] {
            const int ped = PlayerPed();
            if (ped)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedArmour, ped, requested));
        });
    }

    bool PlayerService::QueueSetWantedLevel(int wantedLevel) noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        const int requested = std::clamp(wantedLevel, 0, 5);
        return Game::GameRuntime::Get().Enqueue([requested] {
            const int player = PlayerId();
            if (player < 0) return;
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetPlayerWantedLevel, player, requested, std::int32_t{0}));
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetPlayerWantedLevelNow, player, std::int32_t{0}));
        });
    }

    bool PlayerService::QueueClearWanted() noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        return Game::GameRuntime::Get().Enqueue([] {
            const int player = PlayerId();
            if (player >= 0)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearPlayerWantedLevel, player));
        });
    }

    bool PlayerService::QueueCleanPlayer() noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        return Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (!ped) return;
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearPedBloodDamage, ped));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearPedWetness, ped));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ClearPedEnvDirt, ped));
            static_cast<void>(NativeInvoker::InvokeVoid(NativeId::ResetPedVisibleDamage, ped));
        });
    }

    bool PlayerService::QueueSuicide() noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        return Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (!ped) return;
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetEntityInvincible, ped, std::int32_t{0}, std::int32_t{0}));
            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetEntityHealth, ped, 0, 0, std::uint32_t{0}));
        });
    }

    std::uint32_t PlayerService::Joaat(const std::string& value) noexcept
    {
        std::uint32_t hash{};
        for (unsigned char c : value)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<unsigned char>(c - 'A' + 'a');
            hash += c;
            hash += hash << 10;
            hash ^= hash >> 6;
        }
        hash += hash << 3;
        hash ^= hash >> 11;
        hash += hash << 15;
        return hash;
    }

    bool PlayerService::QueueModelByName(std::string modelName) noexcept
    {
        if (!IsReady() || !NativeReady() || modelName.empty()) return false;
        const auto model = Joaat(modelName);

        return Game::GameRuntime::Get().Enqueue([this, model] {
            const auto inCdImage = NativeInvoker::Invoke<std::int32_t>(NativeId::IsModelInCdimage, model);
            const auto valid = NativeInvoker::Invoke<std::int32_t>(NativeId::IsModelValid, model);
            const auto isPed = NativeInvoker::Invoke<std::int32_t>(NativeId::IsModelAPed, model);
            if (!inCdImage || *inCdImage == 0 || !valid || *valid == 0 || !isPed || *isPed == 0)
            {
                Core::Logger::Get().Warn("self", "Rejected invalid ped model");
                return;
            }

            if (!NativeInvoker::InvokeVoid(NativeId::RequestModel, model))
                return;

            const auto previous = m_PendingModel.exchange(model, std::memory_order_acq_rel);
            if (previous && previous != model)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetModelAsNoLongerNeeded, previous));

            m_ModelAttempts = 0;
            static_cast<void>(EnsureLoop());
        });
    }

    bool PlayerService::QueueSetComponent(int component, int drawable, int texture, int palette) noexcept
    {
        if (!IsReady() || !NativeReady()
            || component < 0 || component > 11
            || drawable < 0 || texture < 0
            || palette < 0 || palette > 3)
        {
            return false;
        }

        return Game::GameRuntime::Get().Enqueue([component, drawable, texture, palette] {
            const int ped = PlayerPed();
            if (!ped) return;

            const auto drawableCount = NativeInvoker::Invoke<std::int32_t>(
                NativeId::GetNumberOfPedDrawableVariations, ped, component);
            if (!drawableCount || drawable >= *drawableCount)
                return;

            const auto textureCount = NativeInvoker::Invoke<std::int32_t>(
                NativeId::GetNumberOfPedTextureVariations, ped, component, drawable);
            if (!textureCount || texture >= *textureCount)
                return;

            static_cast<void>(NativeInvoker::InvokeVoid(
                NativeId::SetPedComponentVariation,
                ped,
                component,
                drawable,
                texture,
                palette));
        });
    }

    bool PlayerService::QueueDefaultComponents() noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        return Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (ped)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedDefaultComponentVariation, ped));
        });
    }

    bool PlayerService::QueueRandomComponents() noexcept
    {
        if (!IsReady() || !NativeReady()) return false;
        return Game::GameRuntime::Get().Enqueue([] {
            const int ped = PlayerPed();
            if (ped)
                static_cast<void>(NativeInvoker::InvokeVoid(NativeId::SetPedRandomComponentVariation, ped, std::int32_t{0}));
        });
    }

    bool PlayerService::GodMode() const noexcept { return m_GodMode.load(std::memory_order_acquire); }
    bool PlayerService::Bulletproof() const noexcept { return m_Bulletproof.load(std::memory_order_acquire); }
    bool PlayerService::Invisible() const noexcept { return m_Invisible.load(std::memory_order_acquire); }
    bool PlayerService::DisableCriticalHits() const noexcept { return m_DisableCriticalHits.load(std::memory_order_acquire); }
    bool PlayerService::KeepPlayerClean() const noexcept { return m_KeepPlayerClean.load(std::memory_order_acquire); }
    bool PlayerService::AquaLungs() const noexcept { return m_AquaLungs.load(std::memory_order_acquire); }
    bool PlayerService::InfiniteOxygen() const noexcept { return m_InfiniteOxygen.load(std::memory_order_acquire); }
    bool PlayerService::NeverWanted() const noexcept { return m_NeverWanted.load(std::memory_order_acquire); }
    bool PlayerService::PoliceIgnore() const noexcept { return m_PoliceIgnore.load(std::memory_order_acquire); }
    bool PlayerService::EveryoneIgnore() const noexcept { return m_EveryoneIgnore.load(std::memory_order_acquire); }
    bool PlayerService::SuperJump() const noexcept { return m_SuperJump.load(std::memory_order_acquire); }
    bool PlayerService::InfiniteStamina() const noexcept { return m_InfiniteStamina.load(std::memory_order_acquire); }
    bool PlayerService::NoRagdoll() const noexcept { return m_NoRagdoll.load(std::memory_order_acquire); }
    bool PlayerService::StandOnVehicles() const noexcept { return m_StandOnVehicles.load(std::memory_order_acquire); }
    bool PlayerService::DisableActionMode() const noexcept { return m_DisableActionMode.load(std::memory_order_acquire); }
    bool PlayerService::InfiniteParachutes() const noexcept { return m_InfiniteParachutes.load(std::memory_order_acquire); }
    bool PlayerService::MobileRadio() const noexcept { return m_MobileRadio.load(std::memory_order_acquire); }
    float PlayerService::RunMultiplier() const noexcept { return m_RunMultiplier.load(std::memory_order_acquire); }
    float PlayerService::SwimMultiplier() const noexcept { return m_SwimMultiplier.load(std::memory_order_acquire); }
    bool PlayerService::ModelLoadPending() const noexcept { return m_PendingModel.load(std::memory_order_acquire) != 0; }
}
