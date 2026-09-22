#include "Application.hpp"

#include "../backend/BackendHub.hpp"
#include "../config/SettingsService.hpp"
#include "../core/Logger.hpp"
#include "../game/GameRuntime.hpp"
#include "../features/player/PlayerService.hpp"
#include "../features/player/PlayerStatsService.hpp"
#include "../features/player/SelfOnlineService.hpp"
#include "../features/protection/ProtectionService.hpp"
#include "../features/online/OnlineStatusService.hpp"
#include "../features/online/OnlinePlayerService.hpp"
#include "../features/utility/UtilityService.hpp"
#include "../features/vehicle/VehicleService.hpp"
#include "../features/weapon/WeaponService.hpp"
#include "../features/world/TeleportService.hpp"
#include "../features/world/WorldService.hpp"
#include "../hooking/HookManager.hpp"
#include "../render/Renderer.hpp"
#include "../ui/Menu.hpp"
#include "../ui/MenuTheme.hpp"

namespace TutonesV2::App
{
    Application& Application::Get() noexcept
    {
        static Application instance;
        return instance;
    }

    bool Application::Initialize(const std::filesystem::path& moduleDirectory) noexcept
    {
        bool expected = false;
        if (!m_Running.compare_exchange_strong(expected, true))
            return true;

        if (!Core::Logger::Get().Initialize(moduleDirectory))
        {
            m_Running.store(false);
            return false;
        }

        Core::Logger::Get().Info("core", "Tutones Menu V2 bootstrap starting");

        if (!Config::SettingsService::Get().Initialize(moduleDirectory))
        {
            Core::Logger::Get().Error("core", "Settings service initialization failed");
            Shutdown();
            return false;
        }

        Features::Protection::ProtectionRuntime::Get().PrepareForStart();
        if (!Features::Protection::ProtectionRuntime::Get().Start())
        {
            Core::Logger::Get().Warn(
                "protections",
                "Protection runtime did not start; V2 will continue without packet protections");
        }

        {
            const auto settings = Config::SettingsService::Get().Snapshot();
            auto& theme = UI::MenuTheme::Get();
            theme.Opacity() = settings.opacity;
            theme.Scale() = settings.scale;
            theme.ShowStatusBar() = settings.showStatusBar;
            auto* accent = theme.AccentColor();
            accent[0] = settings.accent[0];
            accent[1] = settings.accent[1];
            accent[2] = settings.accent[2];
            accent[3] = 1.0f;
            UI::Menu::Get().SetPage(static_cast<UI::MenuPage>(settings.selectedPage));
        }

        if (!Backend::BackendHub::Get().Initialize()
            || !Game::GameRuntime::Get().Initialize()
            || !Features::Player::PlayerService::Get().Initialize()
            || !Features::Player::SelfOnlineService::Get().Initialize()
            || !Features::Player::PlayerStatsService::Get().Initialize()
            || !Features::Online::OnlineStatusService::Get().Initialize()
            || !Features::Online::OnlinePlayerService::Get().Initialize()
            || !Features::Utility::UtilityService::Get().Initialize()
            || !Features::Weapon::WeaponService::Get().Initialize()
            || !Features::Vehicle::VehicleService::Get().Initialize()
            || !Features::World::TeleportService::Get().Initialize()
            || !Features::World::WorldService::Get().Initialize()
            || !Render::Renderer::Get().Initialize()
            || !Hooking::HookManager::Get().Initialize())
        {
            Core::Logger::Get().Error("core", "V2 service initialization failed");
            Shutdown();
            return false;
        }

        {
            const auto settings = Config::SettingsService::Get().Snapshot();
            auto& utilities = Features::Utility::UtilityService::Get();
            utilities.SetShowCoordinates(settings.miscShowCoordinates);
            utilities.SetShowHeading(settings.miscShowHeading);
            utilities.SetShowFps(settings.miscShowFps);
            utilities.SetShowSessionInfo(settings.miscShowSessionInfo);
            utilities.SetDisableCameraShake(settings.miscDisableCameraShake);
        }

        Core::Logger::Get().Info("core", "Tutones Menu V2 DX12 shell ready; Insert or F4 toggles the menu");
        return true;
    }

    void Application::Shutdown() noexcept
    {
        if (!m_Running.exchange(false))
            return;

        Core::Logger::Get().Info("core", "Tutones Menu V2 shutting down");
        Features::Protection::ProtectionRuntime::Get().Stop();
        Features::World::WorldService::Get().Shutdown();
        Features::World::TeleportService::Get().Shutdown();
        Features::Vehicle::VehicleService::Get().Shutdown();
        Features::Weapon::WeaponService::Get().Shutdown();
        Features::Utility::UtilityService::Get().Shutdown();
        Features::Online::OnlinePlayerService::Get().Shutdown();
        Features::Online::OnlineStatusService::Get().Shutdown();
        Features::Player::PlayerStatsService::Get().Shutdown();
        Features::Player::SelfOnlineService::Get().Shutdown();
        Features::Player::PlayerService::Get().Shutdown();
        Hooking::HookManager::Get().Shutdown();
        Render::Renderer::Get().Shutdown();
        Game::GameRuntime::Get().Shutdown();
        Backend::BackendHub::Get().Shutdown();
        Config::SettingsService::Get().Shutdown();
        Core::Logger::Get().Info("core", "Tutones Menu V2 stopped");
        Core::Logger::Get().Shutdown();
    }

    bool Application::IsRunning() const noexcept
    {
        return m_Running.load();
    }
}
