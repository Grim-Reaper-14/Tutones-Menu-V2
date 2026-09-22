#pragma once

#include "../../game/native/NativeCallContext.hpp"

#include <atomic>
#include <mutex>

namespace TutonesV2::Features::Utility
{
    struct UtilitySnapshot final
    {
        bool ready{};
        bool showCoordinates{};
        bool showHeading{};
        bool showFps{};
        bool showSessionInfo{};
        bool disableCameraShake{};
        bool positionReadable{};
        Game::Native::NativeVector3 position{};
        float heading{};
    };

    class UtilityService final
    {
    public:
        static UtilityService& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] UtilitySnapshot Snapshot() const;

        void SetShowCoordinates(bool enabled) noexcept;
        void SetShowHeading(bool enabled) noexcept;
        void SetShowFps(bool enabled) noexcept;
        void SetShowSessionInfo(bool enabled) noexcept;
        void SetDisableCameraShake(bool enabled) noexcept;

        [[nodiscard]] bool AnyOverlayVisible() const noexcept;

    private:
        UtilityService() = default;

        [[nodiscard]] bool NeedsGameLoop() const noexcept;
        void EnsureLoop() noexcept;
        void Tick() noexcept;

        std::atomic_bool m_Ready{};
        std::atomic_bool m_LoopQueued{};
        std::atomic_bool m_ShowCoordinates{};
        std::atomic_bool m_ShowHeading{};
        std::atomic_bool m_ShowFps{};
        std::atomic_bool m_ShowSessionInfo{};
        std::atomic_bool m_DisableCameraShake{};

        mutable std::mutex m_Mutex;
        bool m_PositionReadable{};
        Game::Native::NativeVector3 m_Position{};
        float m_Heading{};
    };
}
