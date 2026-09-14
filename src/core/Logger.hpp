#pragma once

#include <filesystem>
#include <mutex>
#include <string_view>

namespace TutonesV2::Core
{
    class Logger final
    {
    public:
        static Logger& Get() noexcept;

        bool Initialize(const std::filesystem::path& moduleDirectory) noexcept;
        void Shutdown() noexcept;

        void Info(std::string_view component, std::string_view message) noexcept;
        void Warn(std::string_view component, std::string_view message) noexcept;
        void Error(std::string_view component, std::string_view message) noexcept;

    private:
        void Write(std::string_view level, std::string_view component, std::string_view message) noexcept;

        std::mutex m_Mutex;
        std::filesystem::path m_LogPath;
        bool m_Ready{};
    };
}
