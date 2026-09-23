#include "ScriptRuntime.hpp"

#include "../native/NativePointers.hpp"

#include <algorithm>
#include <utility>

namespace TutonesV2::Game::Script
{
    namespace
    {
        [[nodiscard]] std::string BoundedScriptName(
            const char* text,
            std::size_t capacity)
        {
            if (!text || capacity == 0)
                return {};

            std::size_t length{};
            while (length < capacity && text[length] != '\0')
                ++length;
            return std::string(text, length);
        }
    }

    ScriptRuntime& ScriptRuntime::Get() noexcept
    {
        static ScriptRuntime instance;
        return instance;
    }

    bool ScriptRuntime::IsReady() const noexcept
    {
        auto& pointers = Native::NativePointers::Get();
        auto* threads = pointers.ScriptThreads();
        return threads
            && threads->data
            && threads->size <= threads->capacity
            && pointers.ScriptGlobals();
    }

    Types::ScriptThread* ScriptRuntime::FindThread(std::uint32_t scriptHash) const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        auto* threads = Native::NativePointers::Get().ScriptThreads();
        if (!threads || !threads->data || threads->size > threads->capacity)
            return nullptr;

        for (std::uint16_t index = 0; index < threads->size; ++index)
        {
            auto* thread = threads->data[index];
            if (thread
                && thread->context.threadId != 0
                && thread->scriptHash == scriptHash)
            {
                return thread;
            }
        }

        return nullptr;
    }

    Types::ScriptProgram* ScriptRuntime::FindProgram(std::uint32_t scriptHash) const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        auto** programs = Native::NativePointers::Get().ScriptPrograms();
        if (!programs)
            return nullptr;

        for (std::size_t index = 0; index < ScriptProgramCount; ++index)
        {
            auto* program = programs[index];
            if (program && (program->hash == scriptHash || program->nameHash == scriptHash))
                return program;
        }

        return nullptr;
    }

    std::vector<ScriptThreadSnapshot> ScriptRuntime::ThreadsSnapshot() const
    {
        std::vector<ScriptThreadSnapshot> result;
        std::scoped_lock lock(m_Mutex);

        auto* threads = Native::NativePointers::Get().ScriptThreads();
        if (!threads || !threads->data || threads->size > threads->capacity)
            return result;

        result.reserve(threads->size);
        for (std::uint16_t index = 0; index < threads->size; ++index)
        {
            auto* thread = threads->data[index];
            if (!thread || thread->context.threadId == 0)
                continue;

            ScriptThreadSnapshot snapshot{};
            snapshot.threadId = thread->context.threadId;
            snapshot.scriptHash = thread->scriptHash;
            snapshot.scriptName = BoundedScriptName(
                thread->scriptName,
                sizeof(thread->scriptName));
            snapshot.state = thread->context.state;
            snapshot.programCounter = thread->context.programCounter;
            snapshot.framePointer = thread->context.framePointer;
            snapshot.stackPointer = thread->context.stackPointer;
            snapshot.stackSize = thread->context.stackSize;
            snapshot.stackReady = thread->stack != nullptr;
            result.push_back(std::move(snapshot));
        }

        std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
            if (left.scriptName == right.scriptName)
                return left.threadId < right.threadId;
            return left.scriptName < right.scriptName;
        });

        return result;
    }

    std::optional<std::uint64_t> ScriptRuntime::ReadLocalRaw(
        std::uint32_t scriptHash,
        std::size_t index) const noexcept
    {
        std::scoped_lock lock(m_Mutex);

        auto* threads = Native::NativePointers::Get().ScriptThreads();
        if (!threads || !threads->data || threads->size > threads->capacity)
            return std::nullopt;

        for (std::uint16_t threadIndex = 0; threadIndex < threads->size; ++threadIndex)
        {
            auto* thread = threads->data[threadIndex];
            if (!thread
                || thread->context.threadId == 0
                || thread->scriptHash != scriptHash
                || !thread->stack)
            {
                continue;
            }

            if (index >= static_cast<std::size_t>(thread->context.stackSize))
                return std::nullopt;

            return static_cast<const std::uint64_t*>(thread->stack)[index];
        }

        return std::nullopt;
    }

    std::int64_t** ScriptRuntime::Globals() const noexcept
    {
        return Native::NativePointers::Get().ScriptGlobals();
    }

    ScriptVmFn ScriptRuntime::ScriptVm() const noexcept
    {
        return Native::NativePointers::Get().ScriptVm();
    }
}
