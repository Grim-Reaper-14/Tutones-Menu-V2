#include "NativeRegistry.hpp"

#include "NativeHandlerValidation.hpp"
#include "../../core/Logger.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace TutonesV2::Game::Native
{
    namespace
    {
        struct NativeDescriptor final
        {
            NativeHash hash;
            const char* name;
        };

        constexpr std::array<NativeDescriptor, NativeCount> Descriptors{{
#define TUTONES_V2_NATIVE_DESCRIPTOR(id, hash, name) NativeDescriptor{hash, name},
            TUTONES_V2_NATIVE_CATALOG(TUTONES_V2_NATIVE_DESCRIPTOR)
#undef TUTONES_V2_NATIVE_DESCRIPTOR
        }};

        static_assert(Descriptors.size() == NativeCount);

        struct NativeProgram final
        {
            std::byte pad00[0x2C]{};
            std::uint32_t nativeCount{};
            std::byte pad30[0x10]{};
            NativeHandler* nativeEntrypoints{};
            std::byte pad48[0x38]{};
        };

        static_assert(offsetof(NativeProgram, nativeCount) == 0x2C);
        static_assert(offsetof(NativeProgram, nativeEntrypoints) == 0x40);
        static_assert(sizeof(NativeProgram) == 0x80);
    }

    NativeRegistry& NativeRegistry::Get() noexcept
    {
        static NativeRegistry instance;
        return instance;
    }

    bool NativeRegistry::Initialize(InitNativeTablesFn initNativeTables) noexcept
    {
        if (m_Ready.load(std::memory_order_acquire))
            return true;

        if (!initNativeTables)
        {
            Core::Logger::Get().Error("native", "Native registry received a null InitNativeTables pointer");
            return false;
        }

        if (!CanInvokeOnCurrentThread())
        {
            Core::Logger::Get().Error("native", "Native table initialization attempted outside the GTA script thread");
            return false;
        }

        std::array<std::uint64_t, NativeCount> slots{};
        for (std::size_t i = 0; i < Descriptors.size(); ++i)
            slots[i] = Descriptors[i].hash;

        NativeProgram program{};
        program.nativeCount = static_cast<std::uint32_t>(slots.size());
        program.nativeEntrypoints = reinterpret_cast<NativeHandler*>(slots.data());

        initNativeTables(&program);

        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            NativeHandler handler{};
            if (!AssignValidatedHandler(slots[i], handler))
            {
                Core::Logger::Get().Error(
                    "native",
                    std::string("Native handler resolution failed for ") + Descriptors[i].name);
                Shutdown();
                return false;
            }
            m_Handlers[i] = handler;
        }

        m_Ready.store(true, std::memory_order_release);
        Core::Logger::Get().Info(
            "native",
            std::string("Cached ") + std::to_string(NativeCount)
                + " GTA Enhanced native handlers from the V1 focused catalog");
        return true;
    }

    void NativeRegistry::Shutdown() noexcept
    {
        m_Ready.store(false, std::memory_order_release);
        m_Handlers.fill(nullptr);
        m_GameThreadId.store(0, std::memory_order_release);
    }

    void NativeRegistry::MarkGameThread(DWORD threadId) noexcept
    {
        const auto previous = m_GameThreadId.exchange(threadId, std::memory_order_acq_rel);
        if (previous == 0 && threadId != 0)
            Core::Logger::Get().Info("native", "GTA script thread identified for native execution");
    }

    bool NativeRegistry::IsReady() const noexcept
    {
        return m_Ready.load(std::memory_order_acquire);
    }

    bool NativeRegistry::CanInvokeOnCurrentThread() const noexcept
    {
        const auto gameThread = m_GameThreadId.load(std::memory_order_acquire);
        return gameThread != 0 && gameThread == ::GetCurrentThreadId();
    }

    NativeHandler NativeRegistry::Handler(NativeId id) const noexcept
    {
        const auto index = static_cast<std::size_t>(id);
        return index < m_Handlers.size() ? m_Handlers[index] : nullptr;
    }

    const char* NativeRegistry::Name(NativeId id) const noexcept
    {
        const auto index = static_cast<std::size_t>(id);
        return index < Descriptors.size() ? Descriptors[index].name : "UNKNOWN_NATIVE";
    }

    NativeHash NativeRegistry::Hash(NativeId id) const noexcept
    {
        const auto index = static_cast<std::size_t>(id);
        return index < Descriptors.size() ? Descriptors[index].hash : 0;
    }
}
