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

        ClearCommands();
        Core::Logger::Get().Info("backend", "Backend hub initialized with bounded command bus");
        return true;
    }

    void BackendHub::Shutdown() noexcept
    {
        if (!m_Initialized.exchange(false))
            return;

        ClearCommands();
        Core::Logger::Get().Info("backend", "Backend hub stopped");
    }

    bool BackendHub::IsInitialized() const noexcept
    {
        return m_Initialized.load();
    }

    bool BackendHub::Submit(BackendCommand command) noexcept
    {
        if (!IsInitialized())
            return false;

        std::scoped_lock lock(m_CommandMutex);
        if (m_CommandCount >= CommandCapacity)
        {
            Core::Logger::Get().Warn("backend", "Backend command bus is full; request rejected");
            return false;
        }

        const auto tail = (m_CommandHead + m_CommandCount) % CommandCapacity;
        m_Commands[tail] = command;
        ++m_CommandCount;
        return true;
    }

    bool BackendHub::TryPop(BackendCommand& command) noexcept
    {
        std::scoped_lock lock(m_CommandMutex);
        if (m_CommandCount == 0)
            return false;

        command = m_Commands[m_CommandHead];
        m_CommandHead = (m_CommandHead + 1) % CommandCapacity;
        --m_CommandCount;
        return true;
    }

    std::size_t BackendHub::PendingCount() const noexcept
    {
        std::scoped_lock lock(m_CommandMutex);
        return m_CommandCount;
    }

    void BackendHub::ClearCommands() noexcept
    {
        std::scoped_lock lock(m_CommandMutex);
        m_CommandHead = 0;
        m_CommandCount = 0;
    }
}
