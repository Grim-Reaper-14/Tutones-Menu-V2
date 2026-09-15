#include "HookManager.hpp"

#include "../core/Logger.hpp"
#include "../game/GameRuntime.hpp"
#include "../game/native/NativePointers.hpp"
#include "../render/Renderer.hpp"

#include <Windows.h>
#include <MinHook.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>

namespace TutonesV2::Hooking
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

        PresentFn g_OriginalPresent{};
        ResizeBuffersFn g_OriginalResizeBuffers{};
        ExecuteCommandListsFn g_OriginalExecuteCommandLists{};
        Game::Native::RunScriptThreadsFn g_OriginalRunScriptThreads{};

        void* g_PresentTarget{};
        void* g_ResizeBuffersTarget{};
        void* g_ExecuteCommandListsTarget{};
        void* g_RunScriptThreadsTarget{};

        LRESULT CALLBACK ProbeWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            return ::DefWindowProcW(window, message, wParam, lParam);
        }

        HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
        {
            Render::Renderer::Get().OnPresent(swapChain);
            return g_OriginalPresent(swapChain, syncInterval, flags);
        }

        HRESULT STDMETHODCALLTYPE ResizeBuffersDetour(
            IDXGISwapChain* swapChain,
            UINT bufferCount,
            UINT width,
            UINT height,
            DXGI_FORMAT format,
            UINT flags)
        {
            Render::Renderer::Get().OnBeforeResize(swapChain);
            return g_OriginalResizeBuffers(swapChain, bufferCount, width, height, format, flags);
        }

        void STDMETHODCALLTYPE ExecuteCommandListsDetour(
            ID3D12CommandQueue* commandQueue,
            UINT commandListCount,
            ID3D12CommandList* const* commandLists)
        {
            Render::Renderer::Get().CaptureCommandQueue(commandQueue);
            g_OriginalExecuteCommandLists(commandQueue, commandListCount, commandLists);
        }

        bool RunScriptThreadsDetour(int operationsToExecute) noexcept
        {
            bool result{};
            if (g_OriginalRunScriptThreads)
                result = g_OriginalRunScriptThreads(operationsToExecute);

            Game::GameRuntime::Get().OnScriptSchedulerTick();
            return result;
        }

        bool DiscoverDx12Targets(void*& present, void*& resizeBuffers, void*& executeCommandLists) noexcept
        {
            constexpr wchar_t className[] = L"TutonesMenuV2Dx12Probe";
            const HINSTANCE instance = ::GetModuleHandleW(nullptr);

            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = ProbeWindowProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = className;

            const ATOM atom = ::RegisterClassExW(&windowClass);
            const bool registeredHere = atom != 0;
            if (!registeredHere && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return false;

            HWND window = ::CreateWindowExW(
                0,
                className,
                L"Tutones Menu V2 DX12 Probe",
                WS_OVERLAPPEDWINDOW,
                0,
                0,
                100,
                100,
                nullptr,
                nullptr,
                instance,
                nullptr);

            if (!window)
            {
                if (registeredHere)
                    ::UnregisterClassW(className, instance);
                return false;
            }

            bool success = false;
            do
            {
                ComPtr<ID3D12Device> device;
                if (FAILED(::D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
                    break;

                D3D12_COMMAND_QUEUE_DESC queueDesc{};
                queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

                ComPtr<ID3D12CommandQueue> commandQueue;
                if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue))))
                    break;

                ComPtr<IDXGIFactory4> factory;
                if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
                    break;

                DXGI_SWAP_CHAIN_DESC swapChainDesc{};
                swapChainDesc.BufferDesc.Width = 100;
                swapChainDesc.BufferDesc.Height = 100;
                swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                swapChainDesc.SampleDesc.Count = 1;
                swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                swapChainDesc.BufferCount = 2;
                swapChainDesc.OutputWindow = window;
                swapChainDesc.Windowed = TRUE;
                swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

                ComPtr<IDXGISwapChain> swapChain;
                if (FAILED(factory->CreateSwapChain(commandQueue.Get(), &swapChainDesc, &swapChain)))
                    break;

                auto** swapChainVTable = *reinterpret_cast<void***>(swapChain.Get());
                auto** commandQueueVTable = *reinterpret_cast<void***>(commandQueue.Get());
                if (!swapChainVTable || !commandQueueVTable)
                    break;

                present = swapChainVTable[8];
                resizeBuffers = swapChainVTable[13];
                executeCommandLists = commandQueueVTable[10];
                success = present && resizeBuffers && executeCommandLists;
            }
            while (false);

            ::DestroyWindow(window);
            if (registeredHere)
                ::UnregisterClassW(className, instance);
            return success;
        }

        void RemoveInstalledHooks() noexcept
        {
            if (g_PresentTarget)
                static_cast<void>(::MH_RemoveHook(g_PresentTarget));
            if (g_ResizeBuffersTarget)
                static_cast<void>(::MH_RemoveHook(g_ResizeBuffersTarget));
            if (g_ExecuteCommandListsTarget)
                static_cast<void>(::MH_RemoveHook(g_ExecuteCommandListsTarget));
            if (g_RunScriptThreadsTarget)
                static_cast<void>(::MH_RemoveHook(g_RunScriptThreadsTarget));

            g_PresentTarget = nullptr;
            g_ResizeBuffersTarget = nullptr;
            g_ExecuteCommandListsTarget = nullptr;
            g_RunScriptThreadsTarget = nullptr;
            g_OriginalPresent = nullptr;
            g_OriginalResizeBuffers = nullptr;
            g_OriginalExecuteCommandLists = nullptr;
            g_OriginalRunScriptThreads = nullptr;
        }

        bool CreateRequiredHooks() noexcept
        {
            if (::MH_CreateHook(
                    g_PresentTarget,
                    reinterpret_cast<LPVOID>(&PresentDetour),
                    reinterpret_cast<LPVOID*>(&g_OriginalPresent)) != MH_OK)
            {
                return false;
            }

            if (::MH_CreateHook(
                    g_ResizeBuffersTarget,
                    reinterpret_cast<LPVOID>(&ResizeBuffersDetour),
                    reinterpret_cast<LPVOID*>(&g_OriginalResizeBuffers)) != MH_OK)
            {
                return false;
            }

            if (::MH_CreateHook(
                    g_ExecuteCommandListsTarget,
                    reinterpret_cast<LPVOID>(&ExecuteCommandListsDetour),
                    reinterpret_cast<LPVOID*>(&g_OriginalExecuteCommandLists)) != MH_OK)
            {
                return false;
            }

            if (g_RunScriptThreadsTarget
                && ::MH_CreateHook(
                       g_RunScriptThreadsTarget,
                       reinterpret_cast<LPVOID>(&RunScriptThreadsDetour),
                       reinterpret_cast<LPVOID*>(&g_OriginalRunScriptThreads)) != MH_OK)
            {
                return false;
            }

            return true;
        }
    }

    HookManager& HookManager::Get() noexcept
    {
        static HookManager instance;
        return instance;
    }

    bool HookManager::Initialize() noexcept
    {
        bool expected = false;
        if (!m_Initialized.compare_exchange_strong(expected, true))
            return true;

        void* present{};
        void* resizeBuffers{};
        void* executeCommandLists{};
        if (!DiscoverDx12Targets(present, resizeBuffers, executeCommandLists))
        {
            Core::Logger::Get().Error("hooks", "Failed to discover DX12 hook targets");
            m_Initialized.store(false);
            return false;
        }

        const bool nativeEligible = Game::GameRuntime::Get().NativeRuntimeAvailable();
        const auto runScriptThreads = nativeEligible
            ? Game::Native::NativePointers::Get().RunScriptThreads()
            : nullptr;

        if (nativeEligible && !runScriptThreads)
        {
            Core::Logger::Get().Warn(
                "hooks",
                "Native runtime was eligible but RunScriptThreads is unavailable; installing DX12 hooks only");
        }

        const MH_STATUS initStatus = ::MH_Initialize();
        if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            Core::Logger::Get().Error("hooks", "MinHook initialization failed");
            m_Initialized.store(false);
            return false;
        }

        g_PresentTarget = present;
        g_ResizeBuffersTarget = resizeBuffers;
        g_ExecuteCommandListsTarget = executeCommandLists;
        g_RunScriptThreadsTarget = runScriptThreads
            ? reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(runScriptThreads))
            : nullptr;

        if (!CreateRequiredHooks() || ::MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
        {
            static_cast<void>(::MH_DisableHook(MH_ALL_HOOKS));
            RemoveInstalledHooks();
            static_cast<void>(::MH_Uninitialize());
            Core::Logger::Get().Error("hooks", "Failed to install V2 runtime hooks");
            m_Initialized.store(false);
            return false;
        }

        if (g_RunScriptThreadsTarget)
        {
            Game::GameRuntime::Get().MarkSchedulerHookInstalled();
            Core::Logger::Get().Info(
                "hooks",
                "DX12 Present, ResizeBuffers, command-queue and GTA script-scheduler hooks installed");
        }
        else
        {
            Core::Logger::Get().Warn(
                "hooks",
                "DX12 Present, ResizeBuffers and command-queue hooks installed; GTA native scheduler hook is disabled");
        }

        return true;
    }

    void HookManager::Shutdown() noexcept
    {
        if (!m_Initialized.exchange(false))
            return;

        static_cast<void>(::MH_DisableHook(MH_ALL_HOOKS));
        RemoveInstalledHooks();
        static_cast<void>(::MH_Uninitialize());
        Core::Logger::Get().Info("hooks", "V2 runtime hooks removed");
    }

    bool HookManager::IsInitialized() const noexcept
    {
        return m_Initialized.load();
    }
}
