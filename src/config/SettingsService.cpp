#include "SettingsService.hpp"

#include "../core/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace TutonesV2::Config
{
    namespace
    {
        std::string Trim(std::string value)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return {};

            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        bool ParseBool(const std::string& value, bool fallback) noexcept
        {
            if (value == "1" || value == "true" || value == "TRUE" || value == "on")
                return true;
            if (value == "0" || value == "false" || value == "FALSE" || value == "off")
                return false;
            return fallback;
        }

        int ParseInt(const std::string& value, int fallback) noexcept
        {
            try
            {
                return std::stoi(value);
            }
            catch (...)
            {
                return fallback;
            }
        }

        float ParseFloat(const std::string& value, float fallback) noexcept
        {
            try
            {
                return std::stof(value);
            }
            catch (...)
            {
                return fallback;
            }
        }

        std::string ParseString(const std::string& value, std::string fallback)
        {
            std::istringstream stream(value);
            std::string parsed;
            if (stream >> std::quoted(parsed))
                return parsed;

            const auto trimmed = Trim(value);
            return trimmed.empty() ? std::move(fallback) : trimmed;
        }

        void Clamp(MenuSettings& settings) noexcept
        {
            settings.selectedPage = std::clamp(settings.selectedPage, 0, 8);
            settings.opacity = std::clamp(settings.opacity, 0.70f, 1.0f);
            settings.scale = std::clamp(settings.scale, 0.85f, 1.35f);

            for (std::size_t index = 0; index < 3; ++index)
                settings.accent[index] = std::clamp(settings.accent[index], 0.0f, 1.0f);
            settings.accent[3] = 1.0f;

            settings.selfHealth = std::max(0, settings.selfHealth);
            settings.selfArmor = std::clamp(settings.selfArmor, 0, 100);
            settings.selfWantedLevel = std::clamp(settings.selfWantedLevel, 0, 5);
            settings.selfComponent = std::clamp(settings.selfComponent, 0, 11);
            settings.selfDrawable = std::max(0, settings.selfDrawable);
            settings.selfTexture = std::max(0, settings.selfTexture);
            settings.selfPalette = std::clamp(settings.selfPalette, 0, 3);
            settings.teleportDirectionalDistance =
                std::clamp(settings.teleportDirectionalDistance, 1.0f, 100.0f);

            if (settings.selfPedModel.empty())
                settings.selfPedModel = "mp_m_freemode_01";
            if (settings.weaponName.empty())
                settings.weaponName = "WEAPON_CARBINERIFLE";
            if (settings.vehicleModel.empty())
                settings.vehicleModel = "adder";
        }
    }

    SettingsService& SettingsService::Get() noexcept
    {
        static SettingsService instance;
        return instance;
    }

    bool SettingsService::Initialize(const std::filesystem::path& moduleDirectory) noexcept
    {
        bool expected = false;
        if (!m_Initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return true;

        m_Path = moduleDirectory / "Tutones-Menu-V2.ini";
        m_Dirty.store(false, std::memory_order_release);
        m_Stop.store(false, std::memory_order_release);

        if (!Load())
            Core::Logger::Get().Warn("settings", "Settings load failed; defaults will be used");

        try
        {
            m_Worker = std::thread([this] { WorkerMain(); });
        }
        catch (...)
        {
            Core::Logger::Get().Warn("settings", "Autosave worker could not start; manual/shutdown saves remain available");
        }

        Core::Logger::Get().Info(
            "settings",
            std::string("Settings ready at ") + m_Path.string());
        return true;
    }

    void SettingsService::Shutdown() noexcept
    {
        if (!m_Initialized.load(std::memory_order_acquire))
            return;

        m_Stop.store(true, std::memory_order_release);
        m_WorkerCv.notify_all();

        if (m_Worker.joinable())
            m_Worker.join();

        static_cast<void>(SaveNow());

        m_Initialized.store(false, std::memory_order_release);
        m_Dirty.store(false, std::memory_order_release);
        m_Stop.store(false, std::memory_order_release);
    }

    bool SettingsService::IsInitialized() const noexcept
    {
        return m_Initialized.load(std::memory_order_acquire);
    }

    MenuSettings SettingsService::Snapshot() const
    {
        std::scoped_lock lock(m_DataMutex);
        return m_Data;
    }

    std::filesystem::path SettingsService::Path() const
    {
        return m_Path;
    }

    bool SettingsService::Load() noexcept
    {
        std::ifstream input(m_Path);
        if (!input.is_open())
        {
            Core::Logger::Get().Info("settings", "No saved settings file found; using defaults");
            return true;
        }

        MenuSettings loaded{};
        std::string section;
        std::string line;

        while (std::getline(input, line))
        {
            line = Trim(std::move(line));
            if (line.empty() || line.front() == '#' || line.front() == ';')
                continue;

            if (line.front() == '[' && line.back() == ']')
            {
                section = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            const auto equals = line.find('=');
            if (equals == std::string::npos)
                continue;

            const std::string key = Trim(line.substr(0, equals));
            const std::string value = Trim(line.substr(equals + 1));

            if (section == "menu")
            {
                if (key == "selected_page") loaded.selectedPage = ParseInt(value, loaded.selectedPage);
            }
            else if (section == "appearance")
            {
                if (key == "opacity") loaded.opacity = ParseFloat(value, loaded.opacity);
                else if (key == "scale") loaded.scale = ParseFloat(value, loaded.scale);
                else if (key == "show_status_bar") loaded.showStatusBar = ParseBool(value, loaded.showStatusBar);
                else if (key == "accent_r") loaded.accent[0] = ParseFloat(value, loaded.accent[0]);
                else if (key == "accent_g") loaded.accent[1] = ParseFloat(value, loaded.accent[1]);
                else if (key == "accent_b") loaded.accent[2] = ParseFloat(value, loaded.accent[2]);
            }
            else if (section == "self")
            {
                if (key == "health") loaded.selfHealth = ParseInt(value, loaded.selfHealth);
                else if (key == "armor") loaded.selfArmor = ParseInt(value, loaded.selfArmor);
                else if (key == "wanted_level") loaded.selfWantedLevel = ParseInt(value, loaded.selfWantedLevel);
                else if (key == "ped_model") loaded.selfPedModel = ParseString(value, loaded.selfPedModel);
                else if (key == "component") loaded.selfComponent = ParseInt(value, loaded.selfComponent);
                else if (key == "drawable") loaded.selfDrawable = ParseInt(value, loaded.selfDrawable);
                else if (key == "texture") loaded.selfTexture = ParseInt(value, loaded.selfTexture);
                else if (key == "palette") loaded.selfPalette = ParseInt(value, loaded.selfPalette);
            }
            else if (section == "weapons")
            {
                if (key == "weapon_name") loaded.weaponName = ParseString(value, loaded.weaponName);
            }
            else if (section == "vehicle")
            {
                if (key == "model") loaded.vehicleModel = ParseString(value, loaded.vehicleModel);
                else if (key == "enter_after_spawn") loaded.vehicleEnterAfterSpawn = ParseBool(value, loaded.vehicleEnterAfterSpawn);
                else if (key == "networked") loaded.vehicleNetworked = ParseBool(value, loaded.vehicleNetworked);
            }
            else if (section == "teleport")
            {
                if (key == "x") loaded.teleportX = ParseFloat(value, loaded.teleportX);
                else if (key == "y") loaded.teleportY = ParseFloat(value, loaded.teleportY);
                else if (key == "z") loaded.teleportZ = ParseFloat(value, loaded.teleportZ);
                else if (key == "resolve_ground") loaded.teleportResolveGround = ParseBool(value, loaded.teleportResolveGround);
                else if (key == "directional_distance") loaded.teleportDirectionalDistance = ParseFloat(value, loaded.teleportDirectionalDistance);
            }
            else if (section == "misc")
            {
                if (key == "show_coordinates") loaded.miscShowCoordinates = ParseBool(value, loaded.miscShowCoordinates);
                else if (key == "show_heading") loaded.miscShowHeading = ParseBool(value, loaded.miscShowHeading);
                else if (key == "show_fps") loaded.miscShowFps = ParseBool(value, loaded.miscShowFps);
                else if (key == "show_session_info") loaded.miscShowSessionInfo = ParseBool(value, loaded.miscShowSessionInfo);
                else if (key == "disable_camera_shake") loaded.miscDisableCameraShake = ParseBool(value, loaded.miscDisableCameraShake);
            }
        }

        Clamp(loaded);

        {
            std::scoped_lock lock(m_DataMutex);
            m_Data = std::move(loaded);
        }

        Core::Logger::Get().Info("settings", "Saved menu settings loaded");
        return true;
    }

    bool SettingsService::SaveNow() noexcept
    {
        if (m_Path.empty())
            return false;

        const bool saved = SaveSnapshot(Snapshot());
        if (saved)
            m_Dirty.store(false, std::memory_order_release);
        return saved;
    }

    bool SettingsService::SaveSnapshot(const MenuSettings& snapshot) noexcept
    {
        std::ofstream output(m_Path, std::ios::trunc);
        if (!output.is_open())
        {
            Core::Logger::Get().Warn("settings", "Could not open settings file for writing");
            return false;
        }

        output << "# Tutones Menu V2 settings\n";
        output << "# Gameplay feature toggles are intentionally not auto-restored yet.\n\n";

        output << "[menu]\n";
        output << "selected_page=" << snapshot.selectedPage << "\n\n";

        output << "[appearance]\n";
        output << "opacity=" << snapshot.opacity << "\n";
        output << "scale=" << snapshot.scale << "\n";
        output << "show_status_bar=" << (snapshot.showStatusBar ? "true" : "false") << "\n";
        output << "accent_r=" << snapshot.accent[0] << "\n";
        output << "accent_g=" << snapshot.accent[1] << "\n";
        output << "accent_b=" << snapshot.accent[2] << "\n\n";

        output << "[self]\n";
        output << "health=" << snapshot.selfHealth << "\n";
        output << "armor=" << snapshot.selfArmor << "\n";
        output << "wanted_level=" << snapshot.selfWantedLevel << "\n";
        output << "ped_model=" << std::quoted(snapshot.selfPedModel) << "\n";
        output << "component=" << snapshot.selfComponent << "\n";
        output << "drawable=" << snapshot.selfDrawable << "\n";
        output << "texture=" << snapshot.selfTexture << "\n";
        output << "palette=" << snapshot.selfPalette << "\n\n";

        output << "[weapons]\n";
        output << "weapon_name=" << std::quoted(snapshot.weaponName) << "\n\n";

        output << "[vehicle]\n";
        output << "model=" << std::quoted(snapshot.vehicleModel) << "\n";
        output << "enter_after_spawn=" << (snapshot.vehicleEnterAfterSpawn ? "true" : "false") << "\n";
        output << "networked=" << (snapshot.vehicleNetworked ? "true" : "false") << "\n\n";

        output << "[teleport]\n";
        output << "x=" << snapshot.teleportX << "\n";
        output << "y=" << snapshot.teleportY << "\n";
        output << "z=" << snapshot.teleportZ << "\n";
        output << "resolve_ground=" << (snapshot.teleportResolveGround ? "true" : "false") << "\n";
        output << "directional_distance=" << snapshot.teleportDirectionalDistance << "\n\n";

        output << "[misc]\n";
        output << "show_coordinates=" << (snapshot.miscShowCoordinates ? "true" : "false") << "\n";
        output << "show_heading=" << (snapshot.miscShowHeading ? "true" : "false") << "\n";
        output << "show_fps=" << (snapshot.miscShowFps ? "true" : "false") << "\n";
        output << "show_session_info=" << (snapshot.miscShowSessionInfo ? "true" : "false") << "\n";
        output << "disable_camera_shake=" << (snapshot.miscDisableCameraShake ? "true" : "false") << "\n";

        output.flush();
        if (!output.good())
        {
            Core::Logger::Get().Warn("settings", "Settings write did not complete cleanly");
            return false;
        }

        return true;
    }

    void SettingsService::MarkDirty() noexcept
    {
        if (!m_Initialized.load(std::memory_order_acquire))
            return;

        m_Dirty.store(true, std::memory_order_release);
        m_WorkerCv.notify_one();
    }

    void SettingsService::WorkerMain() noexcept
    {
        using namespace std::chrono_literals;

        std::unique_lock lock(m_WorkerMutex);
        while (!m_Stop.load(std::memory_order_acquire))
        {
            m_WorkerCv.wait(lock, [this] {
                return m_Stop.load(std::memory_order_acquire)
                    || m_Dirty.load(std::memory_order_acquire);
            });

            if (m_Stop.load(std::memory_order_acquire))
                break;

            // Debounce rapid slider/input changes so disk I/O never follows every frame.
            m_WorkerCv.wait_for(lock, 750ms, [this] {
                return m_Stop.load(std::memory_order_acquire);
            });

            if (m_Stop.load(std::memory_order_acquire))
                break;

            if (!m_Dirty.exchange(false, std::memory_order_acq_rel))
                continue;

            lock.unlock();
            if (!SaveSnapshot(Snapshot()))
                m_Dirty.store(true, std::memory_order_release);
            lock.lock();
        }
    }
}
