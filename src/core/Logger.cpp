#include "Logger.hpp"

#include <Windows.h>

#include <chrono>
#include <fstream>
#include <format>

namespace TutonesV2::Core
{
    Logger& Logger::Get() noexcept
    {
        static Logger instance;
        return instance;
    }

    bool Logger::Initialize(const std::filesystem::path& moduleDirectory) noexcept
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Ready)
            return true;
        if (moduleDirectory.empty())
            return false;

        std::error_code error;
        const auto logDirectory = moduleDirectory / "logs";
        std::filesystem::create_directories(logDirectory, error);
        if (error)
            return false;

        m_LogPath = logDirectory / "Tutones-Menu-V2.log";
        std::ofstream stream(m_LogPath, std::ios::app);
        if (!stream)
            return false;

        m_Ready = true;
        return true;
    }

    void Logger::Shutdown() noexcept
    {
        std::scoped_lock lock(m_Mutex);
        m_Ready = false;
        m_LogPath.clear();
    }

    void Logger::Info(std::string_view component, std::string_view message) noexcept
    {
        Write("INFO", component, message);
    }

    void Logger::Warn(std::string_view component, std::string_view message) noexcept
    {
        Write("WARN", component, message);
    }

    void Logger::Error(std::string_view component, std::string_view message) noexcept
    {
        Write("ERROR", component, message);
    }

    void Logger::Write(std::string_view level, std::string_view component, std::string_view message) noexcept
    {
        std::scoped_lock lock(m_Mutex);
        if (!m_Ready)
            return;

        const auto now = std::chrono::system_clock::now();
        const auto line = std::format("[{:%Y-%m-%d %H:%M:%S}][{}][{}] {}\n", now, level, component, message);

        std::ofstream stream(m_LogPath, std::ios::app);
        if (stream)
            stream << line;

        ::OutputDebugStringA(line.c_str());
    }
}
