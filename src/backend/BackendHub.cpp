#include "BackendHub.hpp"
#include "../core/Logger.hpp"

namespace TutonesV2::Backend
{
    BackendHub& BackendHub::Get() noexcept
    {
        static BackendHub instance;
        return instance;
    }

    bool BackendHub::Initialize() noexcept
    {
        bool expected = false;
        if (!m_Initialized.compare_exchange_strong(expected, true))
            return true;

        Core::Logger::Get().Info("backend", "Backend hub initialized");
        return true;
    }

    void BackendHub::Shutdown() noexcept
    {
        if (!m_Initialized.exchange(false))
            return;

        Core::Logger::Get().Info("backend", "Backend hub stopped");
    }

    bool BackendHub::IsInitialized() const noexcept
    {
        return m_Initialized.load();
    }
}
