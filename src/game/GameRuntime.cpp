#include "GameRuntime.hpp"
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

        Core::Logger::Get().Info("game", "Game runtime initialized (clean V2 shell)");
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
