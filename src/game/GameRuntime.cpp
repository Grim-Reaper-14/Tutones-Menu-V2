#include "GameRuntime.hpp"
#include "GameProcess.hpp"
#include "../core/Logger.hpp"

namespace TutonesV2::Game
{
    GameRuntime& GameRuntime::Get() noexcept
    {
        static GameRuntime instance;
        return instance;
    }

    bool GameRuntime::Initialize() noexcept
    {
        bool expected = false;
        if (!m_Initialized.compare_exchange_strong(expected, true))
            return true;

        if (!GameProcess::IsEnhancedHost())
        {
            Core::Logger::Get().Error("game", "Tutones Menu V2 must be loaded inside GTA5_Enhanced.exe");
            m_Initialized.store(false);
            return false;
        }

        Core::Logger::Get().Info("game", "GTA5_Enhanced.exe detected in-process");
        return true;
    }

    void GameRuntime::Shutdown() noexcept
    {
        if (!m_Initialized.exchange(false))
            return;

        Core::Logger::Get().Info("game", "Game runtime stopped");
    }

    bool GameRuntime::IsInitialized() const noexcept
    {
        return m_Initialized.load();
    }
}
