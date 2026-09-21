#include "Application.hpp"

#include "../backend/BackendHub.hpp"
#include "../core/Logger.hpp"
#include "../game/GameRuntime.hpp"
#include "../features/player/PlayerService.hpp"
#include "../features/player/PlayerStatsService.hpp"
#include "../features/player/SelfOnlineService.hpp"
#include "../hooking/HookManager.hpp"
#include "../render/Renderer.hpp"

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

        if (!Backend::BackendHub::Get().Initialize()
            || !Game::GameRuntime::Get().Initialize()
            || !Features::Player::PlayerService::Get().Initialize()
            || !Features::Player::SelfOnlineService::Get().Initialize()
            || !Features::Player::PlayerStatsService::Get().Initialize()
            || !Render::Renderer::Get().Initialize()
            || !Hooking::HookManager::Get().Initialize())
        {
            Core::Logger::Get().Error("core", "V2 service initialization failed");
            Shutdown();
            return false;
        }

        Core::Logger::Get().Info("core", "Tutones Menu V2 DX12 shell ready; F5 toggles the menu");
        return true;
    }

    void Application::Shutdown() noexcept
    {
        if (!m_Running.exchange(false))
            return;

        Core::Logger::Get().Info("core", "Tutones Menu V2 shutting down");
        Features::Player::PlayerStatsService::Get().Shutdown();
        Features::Player::SelfOnlineService::Get().Shutdown();
        Features::Player::PlayerService::Get().Shutdown();
        Hooking::HookManager::Get().Shutdown();
        Render::Renderer::Get().Shutdown();
        Game::GameRuntime::Get().Shutdown();
        Backend::BackendHub::Get().Shutdown();
        Core::Logger::Get().Info("core", "Tutones Menu V2 stopped");
        Core::Logger::Get().Shutdown();
    }

    bool Application::IsRunning() const noexcept
    {
        return m_Running.load();
    }
}
