#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace TutonesV2::Features::Player
{
    class PlayerService final
    {
    public:
        static PlayerService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;

        bool SetGodMode(bool enabled) noexcept;
        bool SetBulletproof(bool enabled) noexcept;
        bool SetInvisible(bool enabled) noexcept;
        bool SetDisableCriticalHits(bool enabled) noexcept;
        bool SetKeepPlayerClean(bool enabled) noexcept;
        bool SetAquaLungs(bool enabled) noexcept;
        bool SetInfiniteOxygen(bool enabled) noexcept;
        bool SetNeverWanted(bool enabled) noexcept;
        bool SetPoliceIgnore(bool enabled) noexcept;
        bool SetEveryoneIgnore(bool enabled) noexcept;
        bool SetSuperJump(bool enabled) noexcept;
        bool SetInfiniteStamina(bool enabled) noexcept;
        bool SetNoRagdoll(bool enabled) noexcept;
        bool SetStandOnVehicles(bool enabled) noexcept;
        bool SetDisableActionMode(bool enabled) noexcept;
        bool SetInfiniteParachutes(bool enabled) noexcept;
        bool SetMobileRadio(bool enabled) noexcept;
        bool SetRunMultiplier(float multiplier) noexcept;
        bool SetSwimMultiplier(float multiplier) noexcept;

        bool QueueSetHealth(int health) noexcept;
        bool QueueHeal() noexcept;
        bool QueueSetArmor(int armor) noexcept;
        bool QueueSetWantedLevel(int wantedLevel) noexcept;
        bool QueueClearWanted() noexcept;
        bool QueueCleanPlayer() noexcept;
        bool QueueSuicide() noexcept;
        bool QueueModelByName(std::string modelName) noexcept;
        bool QueueSetComponent(int component, int drawable, int texture, int palette) noexcept;
        bool QueueDefaultComponents() noexcept;
        bool QueueRandomComponents() noexcept;

        [[nodiscard]] bool GodMode() const noexcept;
        [[nodiscard]] bool Bulletproof() const noexcept;
        [[nodiscard]] bool Invisible() const noexcept;
        [[nodiscard]] bool DisableCriticalHits() const noexcept;
        [[nodiscard]] bool KeepPlayerClean() const noexcept;
        [[nodiscard]] bool AquaLungs() const noexcept;
        [[nodiscard]] bool InfiniteOxygen() const noexcept;
        [[nodiscard]] bool NeverWanted() const noexcept;
        [[nodiscard]] bool PoliceIgnore() const noexcept;
        [[nodiscard]] bool EveryoneIgnore() const noexcept;
        [[nodiscard]] bool SuperJump() const noexcept;
        [[nodiscard]] bool InfiniteStamina() const noexcept;
        [[nodiscard]] bool NoRagdoll() const noexcept;
        [[nodiscard]] bool StandOnVehicles() const noexcept;
        [[nodiscard]] bool DisableActionMode() const noexcept;
        [[nodiscard]] bool InfiniteParachutes() const noexcept;
        [[nodiscard]] bool MobileRadio() const noexcept;
        [[nodiscard]] float RunMultiplier() const noexcept;
        [[nodiscard]] float SwimMultiplier() const noexcept;
        [[nodiscard]] bool ModelLoadPending() const noexcept;

    private:
        PlayerService() = default;

        bool EnsureLoop() noexcept;
        void Tick() noexcept;
        void ApplyPersistent() noexcept;
        void ProcessPendingModel() noexcept;
        [[nodiscard]] bool HasPersistentWork() const noexcept;
        [[nodiscard]] static std::uint32_t Joaat(const std::string& value) noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_LoopQueued{};
        std::atomic_bool m_GodMode{};
        std::atomic_bool m_Bulletproof{};
        std::atomic_bool m_Invisible{};
        std::atomic_bool m_DisableCriticalHits{};
        std::atomic_bool m_KeepPlayerClean{};
        std::atomic_bool m_AquaLungs{};
        std::atomic_bool m_InfiniteOxygen{};
        std::atomic_bool m_NeverWanted{};
        std::atomic_bool m_PoliceIgnore{};
        std::atomic_bool m_EveryoneIgnore{};
        std::atomic_bool m_SuperJump{};
        std::atomic_bool m_InfiniteStamina{};
        std::atomic_bool m_NoRagdoll{};
        std::atomic_bool m_StandOnVehicles{};
        std::atomic_bool m_DisableActionMode{};
        std::atomic_bool m_InfiniteParachutes{};
        std::atomic_bool m_MobileRadio{};
        std::atomic<float> m_RunMultiplier{1.0f};
        std::atomic<float> m_SwimMultiplier{1.0f};
        std::atomic_uint32_t m_PendingModel{};
        int m_ModelAttempts{};
    };
}
