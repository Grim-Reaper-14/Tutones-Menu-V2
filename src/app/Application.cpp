#include "Application.hpp"

#include "../backend/BackendHub.hpp"
#include "../core/Logger.hpp"
#include "../game/GameRuntime.hpp"
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
            || !Hooking::HookManager::Get().Initialize()
            || !Render::Renderer::Get().Initialize())
        {
            Core::Logger::Get().Error("core", "V2 service initialization failed");
            Shutdown();
            return false;
        }

        Core::Logger::Get().Info("core", "Tutones Menu V2 clean base ready");
        return true;
    }

    void Application::Shutdown() noexcept
    {
        if (!m_Running.exchange(false))
            return;

        Core::Logger::Get().Info("core", "Tutones Menu V2 shutting down");
        Render::Renderer::Get().Shutdown();
        Hooking::HookManager::Get().Shutdown();
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
