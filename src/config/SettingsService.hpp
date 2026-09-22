#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace TutonesV2::Config
{
    struct MenuSettings final
    {
        int selectedPage{0};

        float opacity{0.96f};
        float scale{1.0f};
        bool showStatusBar{true};
        std::array<float, 4> accent{{0.22f, 0.55f, 0.92f, 1.0f}};

        int selfHealth{200};
        int selfArmor{100};
        int selfWantedLevel{};
        std::string selfPedModel{"mp_m_freemode_01"};
        int selfComponent{};
        int selfDrawable{};
        int selfTexture{};
        int selfPalette{};

        std::string weaponName{"WEAPON_CARBINERIFLE"};

        std::string vehicleModel{"adder"};
        bool vehicleEnterAfterSpawn{true};
        bool vehicleNetworked{true};

        float teleportX{};
        float teleportY{};
        float teleportZ{};
        bool teleportResolveGround{true};
        float teleportDirectionalDistance{5.0f};

        bool miscShowCoordinates{};
        bool miscShowHeading{};
        bool miscShowFps{};
        bool miscShowSessionInfo{};
        bool miscDisableCameraShake{};

        bool selfGodMode{};
        bool selfBulletproof{};
        bool selfInvisible{};
        bool selfDisableCriticalHits{};
        bool selfKeepClean{};
        bool selfNoRagdoll{};
        bool selfNeverWanted{};
        bool selfPoliceIgnore{};
        bool selfEveryoneIgnore{};
        bool selfSuperJump{};
        bool selfInfiniteStamina{};
        bool selfStandOnVehicles{};
        bool selfDisableActionMode{};
        bool selfAquaLungs{};
        bool selfInfiniteOxygen{};
        bool selfInfiniteParachutes{};
        bool selfMobileRadio{};
        float selfRunMultiplier{1.0f};
        float selfSwimMultiplier{1.0f};
        int selfRadarMode{};

        bool weaponInfiniteAmmo{};
        bool weaponInfiniteClip{};
        bool weaponExplosiveAmmo{};
        bool weaponAimbot{};
        bool weaponAimForHead{true};
        bool weaponTargetDrivers{true};
        bool weaponLaserSight{};

        bool vehicleGodMode{};
        bool vehicleKeepClean{};
        bool vehicleHornBoost{};
        bool vehicleSpawnMaxed{};
        int vehicleClassFilter{-1};

        bool teleportAutoWaypoint{};

        bool worldFreezeClock{};
        bool worldBlackout{};
        bool worldWeatherOverride{};
        int worldWeatherIndex{1};
        float worldPedDensity{1.0f};
        float worldScenarioPedDensity{1.0f};
        float worldVehicleDensity{1.0f};
        float worldRandomVehicleDensity{1.0f};
        float worldParkedVehicleDensity{1.0f};
        int worldHour{12};
        int worldMinute{};
    };

    class SettingsService final
    {
    public:
        static SettingsService& Get() noexcept;

        bool Initialize(const std::filesystem::path& moduleDirectory) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] MenuSettings Snapshot() const;
        [[nodiscard]] std::filesystem::path Path() const;

        template<typename Mutator>
        void Update(Mutator&& mutator)
        {
            {
                std::scoped_lock lock(m_DataMutex);
                mutator(m_Data);
            }
            MarkDirty();
        }

        bool SaveNow() noexcept;

    private:
        SettingsService() = default;
        SettingsService(const SettingsService&) = delete;
        SettingsService& operator=(const SettingsService&) = delete;

        bool Load() noexcept;
        bool SaveSnapshot(const MenuSettings& snapshot) noexcept;
        void MarkDirty() noexcept;
        void WorkerMain() noexcept;

        mutable std::mutex m_DataMutex;
        MenuSettings m_Data{};

        std::filesystem::path m_Path;
        std::atomic_bool m_Initialized{};
        std::atomic_bool m_Dirty{};
        std::atomic_bool m_Stop{};

        std::mutex m_WorkerMutex;
        std::condition_variable m_WorkerCv;
        std::thread m_Worker;
    };
}
