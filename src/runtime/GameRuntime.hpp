#pragma once

#include "../game/GameRuntime.hpp"

#include <functional>

namespace TutonesV2::Runtime
{
    class GameRuntime final
    {
    public:
        static GameRuntime& Get() noexcept
        {
            static GameRuntime instance;
            return instance;
        }

        [[nodiscard]] bool IsInitialized() const noexcept
        {
            return Game::GameRuntime::Get().IsInitialized();
        }

        [[nodiscard]] bool IsOnGameThread() const noexcept
        {
            return Game::GameRuntime::Get().IsOnGameThread();
        }

        bool Enqueue(std::function<void()> task)
        {
            return Game::GameRuntime::Get().Enqueue(std::move(task));
        }

    private:
        GameRuntime() = default;
    };
}
