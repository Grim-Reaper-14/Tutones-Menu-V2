#pragma once

#include <atomic>
#include <filesystem>

namespace TutonesV2::App
{
    class Application final
    {
    public:
        static Application& Get() noexcept;

        bool Initialize(const std::filesystem::path& moduleDirectory) noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsRunning() const noexcept;

    private:
        std::atomic_bool m_Running{};
    };
}
