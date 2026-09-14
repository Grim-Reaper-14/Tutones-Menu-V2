#include "Renderer.hpp"
#include "../core/Logger.hpp"

namespace TutonesV2::Render
{
    Renderer& Renderer::Get() noexcept
    {
        static Renderer instance;
        return instance;
    }

    bool Renderer::Initialize() noexcept
    {
        bool expected = false;
        if (!m_Initialized.compare_exchange_strong(expected, true))
            return true;

        Core::Logger::Get().Info("render", "Renderer shell initialized; DX12 hook will be added separately");
        return true;
    }

    void Renderer::Shutdown() noexcept
    {
        if (!m_Initialized.exchange(false))
            return;

        Core::Logger::Get().Info("render", "Renderer stopped");
    }

    bool Renderer::IsInitialized() const noexcept
    {
        return m_Initialized.load();
    }
}
