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
            settings.selectedPage = std::clamp(settings.selectedPage, 0, 11);
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
            settings.selfRunMultiplier = std::clamp(settings.selfRunMultiplier, 1.0f, 1.49f);
            settings.selfSwimMultiplier = std::clamp(settings.selfSwimMultiplier, 1.0f, 1.49f);
            settings.selfRadarMode = std::clamp(settings.selfRadarMode, 0, 2);
            settings.vehicleClassFilter = std::clamp(settings.vehicleClassFilter, -1, 22);
            settings.worldWeatherIndex = std::clamp(settings.worldWeatherIndex, 0, 16);
            settings.worldPedDensity = std::clamp(settings.worldPedDensity, 0.0f, 1.0f);
            settings.worldScenarioPedDensity = std::clamp(settings.worldScenarioPedDensity, 0.0f, 1.0f);
            settings.worldVehicleDensity = std::clamp(settings.worldVehicleDensity, 0.0f, 1.0f);
            settings.worldRandomVehicleDensity = std::clamp(settings.worldRandomVehicleDensity, 0.0f, 1.0f);
            settings.worldParkedVehicleDensity = std::clamp(settings.worldParkedVehicleDensity, 0.0f, 1.0f);
            settings.worldHour = std::clamp(settings.worldHour, 0, 23);
            settings.worldMinute = std::clamp(settings.worldMinute, 0, 59);

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
                else if (key == "clone_inside") loaded.vehicleCloneInside = ParseBool(value, loaded.vehicleCloneInside);
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
            else if (section == "features")
            {
                if (key == "self_god_mode") loaded.selfGodMode = ParseBool(value, loaded.selfGodMode);
                else if (key == "self_bulletproof") loaded.selfBulletproof = ParseBool(value, loaded.selfBulletproof);
                else if (key == "self_invisible") loaded.selfInvisible = ParseBool(value, loaded.selfInvisible);
                else if (key == "self_disable_critical_hits") loaded.selfDisableCriticalHits = ParseBool(value, loaded.selfDisableCriticalHits);
                else if (key == "self_keep_clean") loaded.selfKeepClean = ParseBool(value, loaded.selfKeepClean);
                else if (key == "self_no_ragdoll") loaded.selfNoRagdoll = ParseBool(value, loaded.selfNoRagdoll);
                else if (key == "self_never_wanted") loaded.selfNeverWanted = ParseBool(value, loaded.selfNeverWanted);
                else if (key == "self_police_ignore") loaded.selfPoliceIgnore = ParseBool(value, loaded.selfPoliceIgnore);
                else if (key == "self_everyone_ignore") loaded.selfEveryoneIgnore = ParseBool(value, loaded.selfEveryoneIgnore);
                else if (key == "self_super_jump") loaded.selfSuperJump = ParseBool(value, loaded.selfSuperJump);
                else if (key == "self_infinite_stamina") loaded.selfInfiniteStamina = ParseBool(value, loaded.selfInfiniteStamina);
                else if (key == "self_stand_on_vehicles") loaded.selfStandOnVehicles = ParseBool(value, loaded.selfStandOnVehicles);
                else if (key == "self_disable_action_mode") loaded.selfDisableActionMode = ParseBool(value, loaded.selfDisableActionMode);
                else if (key == "self_aqua_lungs") loaded.selfAquaLungs = ParseBool(value, loaded.selfAquaLungs);
                else if (key == "self_infinite_oxygen") loaded.selfInfiniteOxygen = ParseBool(value, loaded.selfInfiniteOxygen);
                else if (key == "self_infinite_parachutes") loaded.selfInfiniteParachutes = ParseBool(value, loaded.selfInfiniteParachutes);
                else if (key == "self_mobile_radio") loaded.selfMobileRadio = ParseBool(value, loaded.selfMobileRadio);
                else if (key == "self_run_multiplier") loaded.selfRunMultiplier = ParseFloat(value, loaded.selfRunMultiplier);
                else if (key == "self_swim_multiplier") loaded.selfSwimMultiplier = ParseFloat(value, loaded.selfSwimMultiplier);
                else if (key == "self_radar_mode") loaded.selfRadarMode = ParseInt(value, loaded.selfRadarMode);
                else if (key == "weapon_infinite_ammo") loaded.weaponInfiniteAmmo = ParseBool(value, loaded.weaponInfiniteAmmo);
                else if (key == "weapon_infinite_clip") loaded.weaponInfiniteClip = ParseBool(value, loaded.weaponInfiniteClip);
                else if (key == "weapon_explosive_ammo") loaded.weaponExplosiveAmmo = ParseBool(value, loaded.weaponExplosiveAmmo);
                else if (key == "weapon_aimbot") loaded.weaponAimbot = ParseBool(value, loaded.weaponAimbot);
                else if (key == "weapon_aim_for_head") loaded.weaponAimForHead = ParseBool(value, loaded.weaponAimForHead);
                else if (key == "weapon_target_drivers") loaded.weaponTargetDrivers = ParseBool(value, loaded.weaponTargetDrivers);
                else if (key == "weapon_laser_sight") loaded.weaponLaserSight = ParseBool(value, loaded.weaponLaserSight);
                else if (key == "vehicle_god_mode") loaded.vehicleGodMode = ParseBool(value, loaded.vehicleGodMode);
                else if (key == "vehicle_keep_clean") loaded.vehicleKeepClean = ParseBool(value, loaded.vehicleKeepClean);
                else if (key == "vehicle_horn_boost") loaded.vehicleHornBoost = ParseBool(value, loaded.vehicleHornBoost);
                else if (key == "vehicle_spawn_maxed") loaded.vehicleSpawnMaxed = ParseBool(value, loaded.vehicleSpawnMaxed);
                else if (key == "vehicle_class_filter") loaded.vehicleClassFilter = ParseInt(value, loaded.vehicleClassFilter);
                else if (key == "teleport_auto_waypoint") loaded.teleportAutoWaypoint = ParseBool(value, loaded.teleportAutoWaypoint);
                else if (key == "world_freeze_clock") loaded.worldFreezeClock = ParseBool(value, loaded.worldFreezeClock);
                else if (key == "world_blackout") loaded.worldBlackout = ParseBool(value, loaded.worldBlackout);
                else if (key == "world_weather_override") loaded.worldWeatherOverride = ParseBool(value, loaded.worldWeatherOverride);
                else if (key == "world_weather_index") loaded.worldWeatherIndex = ParseInt(value, loaded.worldWeatherIndex);
                else if (key == "world_ped_density") loaded.worldPedDensity = ParseFloat(value, loaded.worldPedDensity);
                else if (key == "world_scenario_ped_density") loaded.worldScenarioPedDensity = ParseFloat(value, loaded.worldScenarioPedDensity);
                else if (key == "world_vehicle_density") loaded.worldVehicleDensity = ParseFloat(value, loaded.worldVehicleDensity);
                else if (key == "world_random_vehicle_density") loaded.worldRandomVehicleDensity = ParseFloat(value, loaded.worldRandomVehicleDensity);
                else if (key == "world_parked_vehicle_density") loaded.worldParkedVehicleDensity = ParseFloat(value, loaded.worldParkedVehicleDensity);
                else if (key == "world_hour") loaded.worldHour = ParseInt(value, loaded.worldHour);
                else if (key == "world_minute") loaded.worldMinute = ParseInt(value, loaded.worldMinute);
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
        output << "# Gameplay feature toggles are restored after the native runtime reaches Ready.\n\n";

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
        output << "networked=" << (snapshot.vehicleNetworked ? "true" : "false") << "\n";
        output << "clone_inside=" << (snapshot.vehicleCloneInside ? "true" : "false") << "\n\n";

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
        output << "disable_camera_shake=" << (snapshot.miscDisableCameraShake ? "true" : "false") << "\n\n";

        output << "[features]\n";
        output << "self_god_mode=" << (snapshot.selfGodMode ? "true" : "false") << "\n";
        output << "self_bulletproof=" << (snapshot.selfBulletproof ? "true" : "false") << "\n";
        output << "self_invisible=" << (snapshot.selfInvisible ? "true" : "false") << "\n";
        output << "self_disable_critical_hits=" << (snapshot.selfDisableCriticalHits ? "true" : "false") << "\n";
        output << "self_keep_clean=" << (snapshot.selfKeepClean ? "true" : "false") << "\n";
        output << "self_no_ragdoll=" << (snapshot.selfNoRagdoll ? "true" : "false") << "\n";
        output << "self_never_wanted=" << (snapshot.selfNeverWanted ? "true" : "false") << "\n";
        output << "self_police_ignore=" << (snapshot.selfPoliceIgnore ? "true" : "false") << "\n";
        output << "self_everyone_ignore=" << (snapshot.selfEveryoneIgnore ? "true" : "false") << "\n";
        output << "self_super_jump=" << (snapshot.selfSuperJump ? "true" : "false") << "\n";
        output << "self_infinite_stamina=" << (snapshot.selfInfiniteStamina ? "true" : "false") << "\n";
        output << "self_stand_on_vehicles=" << (snapshot.selfStandOnVehicles ? "true" : "false") << "\n";
        output << "self_disable_action_mode=" << (snapshot.selfDisableActionMode ? "true" : "false") << "\n";
        output << "self_aqua_lungs=" << (snapshot.selfAquaLungs ? "true" : "false") << "\n";
        output << "self_infinite_oxygen=" << (snapshot.selfInfiniteOxygen ? "true" : "false") << "\n";
        output << "self_infinite_parachutes=" << (snapshot.selfInfiniteParachutes ? "true" : "false") << "\n";
        output << "self_mobile_radio=" << (snapshot.selfMobileRadio ? "true" : "false") << "\n";
        output << "self_run_multiplier=" << snapshot.selfRunMultiplier << "\n";
        output << "self_swim_multiplier=" << snapshot.selfSwimMultiplier << "\n";
        output << "self_radar_mode=" << snapshot.selfRadarMode << "\n";
        output << "weapon_infinite_ammo=" << (snapshot.weaponInfiniteAmmo ? "true" : "false") << "\n";
        output << "weapon_infinite_clip=" << (snapshot.weaponInfiniteClip ? "true" : "false") << "\n";
        output << "weapon_explosive_ammo=" << (snapshot.weaponExplosiveAmmo ? "true" : "false") << "\n";
        output << "weapon_aimbot=" << (snapshot.weaponAimbot ? "true" : "false") << "\n";
        output << "weapon_aim_for_head=" << (snapshot.weaponAimForHead ? "true" : "false") << "\n";
        output << "weapon_target_drivers=" << (snapshot.weaponTargetDrivers ? "true" : "false") << "\n";
        output << "weapon_laser_sight=" << (snapshot.weaponLaserSight ? "true" : "false") << "\n";
        output << "vehicle_god_mode=" << (snapshot.vehicleGodMode ? "true" : "false") << "\n";
        output << "vehicle_keep_clean=" << (snapshot.vehicleKeepClean ? "true" : "false") << "\n";
        output << "vehicle_horn_boost=" << (snapshot.vehicleHornBoost ? "true" : "false") << "\n";
        output << "vehicle_spawn_maxed=" << (snapshot.vehicleSpawnMaxed ? "true" : "false") << "\n";
        output << "vehicle_class_filter=" << snapshot.vehicleClassFilter << "\n";
        output << "teleport_auto_waypoint=" << (snapshot.teleportAutoWaypoint ? "true" : "false") << "\n";
        output << "world_freeze_clock=" << (snapshot.worldFreezeClock ? "true" : "false") << "\n";
        output << "world_blackout=" << (snapshot.worldBlackout ? "true" : "false") << "\n";
        output << "world_weather_override=" << (snapshot.worldWeatherOverride ? "true" : "false") << "\n";
        output << "world_weather_index=" << snapshot.worldWeatherIndex << "\n";
        output << "world_ped_density=" << snapshot.worldPedDensity << "\n";
        output << "world_scenario_ped_density=" << snapshot.worldScenarioPedDensity << "\n";
        output << "world_vehicle_density=" << snapshot.worldVehicleDensity << "\n";
        output << "world_random_vehicle_density=" << snapshot.worldRandomVehicleDensity << "\n";
        output << "world_parked_vehicle_density=" << snapshot.worldParkedVehicleDensity << "\n";
        output << "world_hour=" << snapshot.worldHour << "\n";
        output << "world_minute=" << snapshot.worldMinute << "\n";

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
