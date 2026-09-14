#pragma once

#include "NativeCallContext.hpp"
#include "NativeCatalog.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace TutonesV2::Game::Native
{
    using InitNativeTablesFn = void(*)(void* program);

    enum class NativeId : std::size_t
    {
#define TUTONES_V2_NATIVE_ENUM(id, hash, name) id,
        TUTONES_V2_NATIVE_CATALOG(TUTONES_V2_NATIVE_ENUM)
#undef TUTONES_V2_NATIVE_ENUM
        Count,
    };

    inline constexpr std::size_t NativeCount = static_cast<std::size_t>(NativeId::Count);

    class NativeRegistry final
    {
    public:
        static NativeRegistry& Get() noexcept;

        bool Initialize(InitNativeTablesFn initNativeTables) noexcept;
        void Shutdown() noexcept;

        void MarkGameThread(DWORD threadId) noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] bool CanInvokeOnCurrentThread() const noexcept;
        [[nodiscard]] NativeHandler Handler(NativeId id) const noexcept;
        [[nodiscard]] const char* Name(NativeId id) const noexcept;
        [[nodiscard]] NativeHash Hash(NativeId id) const noexcept;

    private:
        std::array<NativeHandler, NativeCount> m_Handlers{};
        std::atomic_bool m_Ready{};
        std::atomic<DWORD> m_GameThreadId{};
    };
}
