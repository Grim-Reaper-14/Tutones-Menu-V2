#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace TutonesV2::Render
{
    class Renderer final
    {
    public:
        static Renderer& Get() noexcept;

        bool Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        void CaptureCommandQueue(ID3D12CommandQueue* commandQueue) noexcept;
        void OnPresent(IDXGISwapChain* swapChain) noexcept;
        void OnBeforeResize(IDXGISwapChain* swapChain) noexcept;

    private:
        struct FrameContext final
        {
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator;
            Microsoft::WRL::ComPtr<ID3D12Resource> BackBuffer;
            D3D12_CPU_DESCRIPTOR_HANDLE Rtv{};
            std::uint64_t FenceValue{};
        };

        bool SelectPrimarySwapChain(IDXGISwapChain* swapChain) noexcept;
        bool QueueMatchesPrimaryDevice(ID3D12CommandQueue* commandQueue) const noexcept;
        bool InitializeSwapChain(IDXGISwapChain* swapChain) noexcept;
        void RenderMenuFrame() noexcept;
        void WaitForOverlayIdle() noexcept;
        void ResetSwapChainState() noexcept;
        void ReleasePrimarySelection() noexcept;

        std::atomic_bool m_Initialized{};
        std::atomic_bool m_RenderReady{};
        std::atomic_bool m_F5Down{};
        std::atomic<IDXGISwapChain*> m_PrimarySwapChain{};
        std::atomic<ID3D12Device*> m_PrimaryDevice{};
        std::atomic<ID3D12CommandQueue*> m_CommandQueue{};
        std::mutex m_StateMutex;

        Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;
        Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;
        Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
        std::vector<FrameContext> m_Frames;

        HWND m_Window{};
        HANDLE m_FenceEvent{};
        DXGI_FORMAT m_BackBufferFormat{DXGI_FORMAT_UNKNOWN};
        UINT m_RtvDescriptorSize{};
        std::uint64_t m_NextFenceValue{};
        bool m_ImGuiReady{};
    };
}
