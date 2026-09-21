#pragma once

#include "../../game/memory/BytePatch.hpp"
#include "../../game/memory/ModuleView.hpp"

namespace TutonesV2::Features::Weapon
{
    class WeaponAimPatches final
    {
    public:
        static WeaponAimPatches& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool AimbotSupported() const noexcept;
        [[nodiscard]] bool AimForHeadSupported() const noexcept;
        [[nodiscard]] bool TargetDriversSupported() const noexcept;

        bool ApplyAimbot(bool enabled) noexcept;
        bool ApplyAimForHead(bool enabled) noexcept;
        bool ApplyTargetDrivers(bool enabled) noexcept;
        void RestoreAll() noexcept;

    private:
        WeaponAimPatches() = default;

        Game::Memory::ModuleView m_Module;
        Game::Memory::BytePatch m_ShouldNotTargetEntity;
        Game::Memory::BytePatch m_GetAssistedAimType;
        Game::Memory::BytePatch m_GetLockOnPos;
        Game::Memory::BytePatch m_ShouldAllowDriverLockOn;
    };
}
