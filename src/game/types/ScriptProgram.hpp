#pragma once

#include "../native/NativeCallContext.hpp"

#include <cstddef>
#include <cstdint>

namespace TutonesV2::Game::Types
{
    struct ScriptProgram final
    {
        std::byte pad00[0x10]{};
        std::uint8_t** codeBlocks{};
        std::uint32_t hash{};
        std::uint32_t codeSize{};
        std::uint32_t argCount{};
        std::uint32_t localCount{};
        std::uint32_t globalCount{};
        std::uint32_t nativeCount{};
        void* localData{};
        void** globalData{};
        Native::NativeHandler* nativeEntrypoints{};
        std::uint32_t procCount{};
        std::byte pad4C[0x4]{};
        const char** procNames{};
        std::uint32_t nameHash{};
        std::uint32_t refCount{};
        const char* name{};
        const char** stringsData{};
        std::uint32_t stringsCount{};
        std::byte pad74[0x0C]{};

        [[nodiscard]] std::uint8_t* GetCodeAddress(std::uint32_t index) const noexcept
        {
            if (!codeBlocks || index >= codeSize)
                return nullptr;
            auto* page = codeBlocks[index >> 14];
            return page ? &page[index & 0x3FFF] : nullptr;
        }
    };

    static_assert(offsetof(ScriptProgram, codeBlocks) == 0x10);
    static_assert(offsetof(ScriptProgram, nativeCount) == 0x2C);
    static_assert(offsetof(ScriptProgram, nativeEntrypoints) == 0x40);
    static_assert(sizeof(ScriptProgram) == 0x80);
}
