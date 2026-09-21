#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace TutonesV2::Features::Weapon
{
    class WeaponService final
    {
    public:
        static WeaponService& Get() noexcept;
        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        bool SetInfiniteAmmo(bool enabled) noexcept;
        bool SetInfiniteClip(bool enabled) noexcept;
        bool SetExplosiveAmmo(bool enabled) noexcept;
        bool SetAimbot(bool enabled) noexcept;
        bool SetAimForHead(bool enabled) noexcept;
        bool SetTargetDrivers(bool enabled) noexcept;
        bool SetLaserSight(bool enabled) noexcept;
        bool QueueGiveAllWeapons() noexcept;
        bool QueueMaxAmmo() noexcept;
        bool QueueGiveWeapon(std::string name) noexcept;
        [[nodiscard]] bool InfiniteAmmo() const noexcept;
        [[nodiscard]] bool InfiniteClip() const noexcept;
        [[nodiscard]] bool ExplosiveAmmo() const noexcept;
        [[nodiscard]] bool Aimbot() const noexcept;
        [[nodiscard]] bool AimForHead() const noexcept;
        [[nodiscard]] bool TargetDrivers() const noexcept;
        [[nodiscard]] bool LaserSight() const noexcept;
        [[nodiscard]] bool AimbotSupported() const noexcept;
        [[nodiscard]] bool AimForHeadSupported() const noexcept;
        [[nodiscard]] bool TargetDriversSupported() const noexcept;
    private:
        WeaponService() = default;
        bool EnsureLoop() noexcept;
        void Tick() noexcept;
        void ApplyAimState() noexcept;
        bool HasPersistentWork() const noexcept;
        static std::uint32_t Joaat(const char* text) noexcept;
        std::atomic_bool m_Ready{};
        std::atomic_bool m_LoopQueued{};
        std::atomic_bool m_InfiniteAmmo{};
        std::atomic_bool m_InfiniteClip{};
        std::atomic_bool m_ExplosiveAmmo{};
        std::atomic_bool m_Aimbot{};
        std::atomic_bool m_AimForHead{true};
        std::atomic_bool m_TargetDrivers{true};
        std::atomic_bool m_LaserSight{};
    };
}
