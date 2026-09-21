#include "WeaponAimPatches.hpp"

#include "../../core/Logger.hpp"
#include "../../game/memory/PatternScanner.hpp"

#include <array>

namespace TutonesV2::Features::Weapon
{
    WeaponAimPatches& WeaponAimPatches::Get() noexcept
    {
        static WeaponAimPatches instance;
        return instance;
    }

    bool WeaponAimPatches::Initialize() noexcept
    {
        Shutdown();

        if (!m_Module.Initialize(L"GTA5_Enhanced.exe"))
        {
            Core::Logger::Get().Warn("weapon.aim", "Could not inspect GTA5_Enhanced.exe for aimbot patches");
            return false;
        }

        bool anyConfigured = false;

        if (auto* match = Game::Memory::PatternScanner::FindFirst(m_Module, "F6 80 A9 14 00 00 01"))
        {
            auto* address = match - 0x53;
            constexpr std::array<std::uint8_t, 3> replacement{0xB0, 0x00, 0xC3};
            if (m_Module.Contains(address) && m_ShouldNotTargetEntity.Configure(address, replacement))
            {
                anyConfigured = true;
                Core::Logger::Get().Info("weapon.aim", "ShouldNotTargetEntity aimbot patch configured");
            }
        }

        if (auto* match = Game::Memory::PatternScanner::FindFirst(m_Module, "FF E0 48 8D 86"))
        {
            auto* address = match - 0x15;
            constexpr std::array<std::uint8_t, 5> replacement{0xBD, 0x01, 0x00, 0x00, 0x00};
            if (m_Module.Contains(address) && m_GetAssistedAimType.Configure(address, replacement))
            {
                anyConfigured = true;
                Core::Logger::Get().Info("weapon.aim", "GetAssistedAimType aimbot patch configured");
            }
        }

        if (auto* match = Game::Memory::PatternScanner::FindFirst(m_Module, "0F 29 74 24 ? 48 89 D6 48 89 CF 48 8B 05"))
        {
            auto* address = match + 0x22;
            constexpr std::array<std::uint8_t, 1> replacement{0xEB};
            if (m_Module.Contains(address) && m_GetLockOnPos.Configure(address, replacement))
            {
                anyConfigured = true;
                Core::Logger::Get().Info("weapon.aim", "GetLockOnPos head-lock patch configured");
            }
        }

        if (auto* match = Game::Memory::PatternScanner::FindFirst(m_Module, "75 ? 45 89 C7 49 89 CE"))
        {
            auto* address = match - 0x2C;
            constexpr std::array<std::uint8_t, 3> replacement{0xB0, 0x01, 0xC3};
            if (m_Module.Contains(address) && m_ShouldAllowDriverLockOn.Configure(address, replacement))
            {
                anyConfigured = true;
                Core::Logger::Get().Info("weapon.aim", "ShouldAllowDriverLockOn patch configured");
            }
        }

        if (!AimbotSupported())
            Core::Logger::Get().Warn("weapon.aim", "Base aimbot patch pair is incomplete; Aimbot will stay unavailable");
        if (!AimForHeadSupported())
            Core::Logger::Get().Warn("weapon.aim", "Aim-for-head patch is unavailable");
        if (!TargetDriversSupported())
            Core::Logger::Get().Warn("weapon.aim", "Target-drivers patch is unavailable");

        return anyConfigured;
    }

    void WeaponAimPatches::Shutdown() noexcept
    {
        RestoreAll();
        m_ShouldNotTargetEntity.Reset();
        m_GetAssistedAimType.Reset();
        m_GetLockOnPos.Reset();
        m_ShouldAllowDriverLockOn.Reset();
        m_Module.Reset();
    }

    bool WeaponAimPatches::AimbotSupported() const noexcept
    {
        return m_ShouldNotTargetEntity.IsConfigured() && m_GetAssistedAimType.IsConfigured();
    }

    bool WeaponAimPatches::AimForHeadSupported() const noexcept
    {
        return m_GetLockOnPos.IsConfigured();
    }

    bool WeaponAimPatches::TargetDriversSupported() const noexcept
    {
        return m_ShouldAllowDriverLockOn.IsConfigured();
    }

    bool WeaponAimPatches::ApplyAimbot(bool enabled) noexcept
    {
        if (!AimbotSupported())
            return false;

        if (enabled)
            return m_ShouldNotTargetEntity.Apply() && m_GetAssistedAimType.Apply();

        const bool one = m_ShouldNotTargetEntity.Restore();
        const bool two = m_GetAssistedAimType.Restore();
        return one && two;
    }

    bool WeaponAimPatches::ApplyAimForHead(bool enabled) noexcept
    {
        if (!AimForHeadSupported())
            return !enabled;
        return enabled ? m_GetLockOnPos.Apply() : m_GetLockOnPos.Restore();
    }

    bool WeaponAimPatches::ApplyTargetDrivers(bool enabled) noexcept
    {
        if (!TargetDriversSupported())
            return !enabled;
        return enabled ? m_ShouldAllowDriverLockOn.Apply() : m_ShouldAllowDriverLockOn.Restore();
    }

    void WeaponAimPatches::RestoreAll() noexcept
    {
        static_cast<void>(m_ShouldNotTargetEntity.Restore());
        static_cast<void>(m_GetAssistedAimType.Restore());
        static_cast<void>(m_GetLockOnPos.Restore());
        static_cast<void>(m_ShouldAllowDriverLockOn.Restore());
    }
}
