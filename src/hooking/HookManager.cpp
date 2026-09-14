#include "HookManager.hpp"
#include "../core/Logger.hpp"

namespace TutonesV2::Hooking
{
    HookManager& HookManager::Get() noexcept
    {
        static HookManager instance;
        return instance;
    }

    bool HookManager::Initialize() noexcept
    {
        bool expected = false;
        if (!m_Initialized.compare_exchange_strong(expected, true))
            return true;

        Core::Logger::Get().Info("hooks", "Hook manager initialized; no render/game hooks installed yet");
        return true;
    }

    void HookManager::Shutdown() noexcept
    {
        if (!m_Initialized.exchange(false))
            return;

        Core::Logger::Get().Info("hooks", "Hook manager stopped");
    }

    bool HookManager::IsInitialized() const noexcept
    {
        return m_Initialized.load();
    }
}
