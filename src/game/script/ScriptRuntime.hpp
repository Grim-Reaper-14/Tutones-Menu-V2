#pragma once

#include "../types/ScriptProgram.hpp"
#include "../types/ScriptTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace TutonesV2::Game::Script
{
    using ScriptVmFn = int(*)(
        std::uint64_t* stack,
        std::int64_t** globals,
        Types::ScriptProgram* program,
        void* context);
    struct ScriptThreadSnapshot final
    {
        std::uint32_t threadId{};
        std::uint32_t scriptHash{};
        std::string scriptName{};
        Types::ScriptThreadState state{Types::ScriptThreadState::Idle};
        std::uint32_t programCounter{};
        std::uint32_t framePointer{};
        std::uint32_t stackPointer{};
        std::uint32_t stackSize{};
        bool stackReady{};
    };

    class ScriptRuntime final
    {
    public:
        static ScriptRuntime& Get() noexcept;

        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] Types::ScriptThread* FindThread(std::uint32_t scriptHash) const noexcept;
        [[nodiscard]] Types::ScriptProgram* FindProgram(std::uint32_t scriptHash) const noexcept;
        [[nodiscard]] std::vector<ScriptThreadSnapshot> ThreadsSnapshot() const;
        [[nodiscard]] std::optional<std::uint64_t> ReadLocalRaw(
            std::uint32_t scriptHash,
            std::size_t index) const noexcept;
        [[nodiscard]] std::int64_t** Globals() const noexcept;
        [[nodiscard]] ScriptVmFn ScriptVm() const noexcept;

    private:
        static constexpr std::size_t ScriptProgramCount = 176;
        ScriptRuntime() = default;
        mutable std::mutex m_Mutex;
    };
}
