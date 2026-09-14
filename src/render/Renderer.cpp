#include "Renderer.hpp"

#include "../core/Logger.hpp"
#include "../ui/Menu.hpp"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace TutonesV2::Render
{
    namespace
    {
        LRESULT CALLBACK OverlayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            return Renderer::Get().HandleWindowMessage(window, message, wParam, lParam);
        }

        bool IsMouseMessage(UINT message) noexcept
        {
            return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
                || message == WM_NCMOUSEMOVE
                || message == WM_NCLBUTTONDOWN
                || message == WM_NCLBUTTONUP
                || message == WM_NCRBUTTONDOWN
                || message == WM_NCRBUTTONUP;
        }

        bool IsKeyboardMessage(UINT message) noexcept
        {
            return message == WM_KEYDOWN
                || message == WM_KEYUP
                || message == WM_SYSKEYDOWN
                || message == WM_SYSKEYUP
                || message == WM_CHAR
                || message == WM_SYSCHAR;
        }
    }

    Renderer& Renderer::Get() noexcept
    {
        static Renderer instance;
        return instance;
    }

    bool Renderer::Initialize() noexcept
    {
        bool expected = false;
        if (!m_Initialized.compare_exchange_strong(expected, true))
            return true;

        Core::Logger::Get().Info("render", "Renderer shell initialized; waiting for GTA DX12 swap chain");
        return true;
    }

    void Renderer::Shutdown() noexcept
    {
        if (!m_Initialized.exchange(false))
            return;

        {
            std::scoped_lock lock(m_StateMutex);
            ResetSwapChainState(true);
        }

        if (ID3D12CommandQueue* commandQueue = m_CommandQueue.exchange(nullptr))
            commandQueue->Release();

        Core::Logger::Get().Info("render", "Renderer stopped");
    }

    bool Renderer::IsInitialized() const noexcept
    {
        return m_Initialized.load();
    }

    void Renderer::CaptureCommandQueue(ID3D12CommandQueue* commandQueue) noexcept
    {
        if (!m_Initialized.load() || !commandQueue)
            return;

        if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
            return;

        ID3D12CommandQueue* expected = nullptr;
        commandQueue->AddRef();
        if (!m_CommandQueue.compare_exchange_strong(expected, commandQueue))
        {
            commandQueue->Release();
            return;
        }

        Core::Logger::Get().Info("render", "Captured GTA direct command queue");
    }

    void Renderer::OnPresent(IDXGISwapChain* swapChain) noexcept
    {
        if (!m_Initialized.load() || !swapChain)
            return;

        if (!m_RenderReady.load(std::memory_order_acquire))
        {
            if (!m_CommandQueue.load())
                return;

            std::scoped_lock lock(m_StateMutex);
            if (!m_RenderReady.load(std::memory_order_relaxed) && !InitializeSwapChain(swapChain))
                return;
        }

        if (!UI::Menu::Get().IsOpen())
            return;

        RenderMenuFrame();
    }

    void Renderer::OnBeforeResize(IDXGISwapChain* swapChain) noexcept
    {
        if (!m_Initialized.load() || !swapChain)
            return;

        std::scoped_lock lock(m_StateMutex);
        if (m_SwapChain && static_cast<IDXGISwapChain*>(m_SwapChain.Get()) == swapChain)
        {
            Core::Logger::Get().Info("render", "DX12 swap chain resize detected; releasing overlay resources");
            ResetSwapChainState(true);
        }
    }

    LRESULT Renderer::HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
        if (message == WM_KEYUP && wParam == VK_F5)
        {
            UI::Menu::Get().Toggle();
            return 0;
        }

        if (m_ImGuiReady && UI::Menu::Get().IsOpen())
        {
            const LRESULT handled = ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
            ImGuiIO& io = ImGui::GetIO();
            if (handled != 0
                || (IsMouseMessage(message) && io.WantCaptureMouse)
                || (IsKeyboardMessage(message) && io.WantCaptureKeyboard))
            {
                return 1;
            }
        }

        if (m_OriginalWindowProc)
            return ::CallWindowProcW(m_OriginalWindowProc, window, message, wParam, lParam);

        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    bool Renderer::InitializeSwapChain(IDXGISwapChain* swapChain) noexcept
    {
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
        if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))))
            return false;

        DXGI_SWAP_CHAIN_DESC description{};
        if (FAILED(swapChain3->GetDesc(&description)) || !description.OutputWindow || description.BufferCount == 0)
            return false;

        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(swapChain3->GetDevice(IID_PPV_ARGS(&device))))
            return false;

        m_SwapChain = swapChain3;
        m_Device = device;
        m_Window = description.OutputWindow;
        m_BackBufferFormat = description.BufferDesc.Format;

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
        rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDescription.NumDescriptors = description.BufferCount;
        if (FAILED(m_Device->CreateDescriptorHeap(&rtvHeapDescription, IID_PPV_ARGS(&m_RtvHeap))))
        {
            ResetSwapChainState(false);
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDescription{};
        srvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDescription.NumDescriptors = 1;
        srvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_Device->CreateDescriptorHeap(&srvHeapDescription, IID_PPV_ARGS(&m_SrvHeap))))
        {
            ResetSwapChainState(false);
            return false;
        }

        m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_Frames.clear();
        m_Frames.resize(description.BufferCount);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < description.BufferCount; ++index)
        {
            FrameContext& frame = m_Frames[index];
            if (FAILED(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.CommandAllocator)))
                || FAILED(m_SwapChain->GetBuffer(index, IID_PPV_ARGS(&frame.BackBuffer))))
            {
                ResetSwapChainState(false);
                return false;
            }

            frame.Rtv = rtv;
            m_Device->CreateRenderTargetView(frame.BackBuffer.Get(), nullptr, frame.Rtv);
            rtv.ptr += m_RtvDescriptorSize;
        }

        if (FAILED(m_Device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                m_Frames.front().CommandAllocator.Get(),
                nullptr,
                IID_PPV_ARGS(&m_CommandList))))
        {
            ResetSwapChainState(false);
            return false;
        }
        static_cast<void>(m_CommandList->Close());

        if (FAILED(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence))))
        {
            ResetSwapChainState(false);
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplWin32_Init(m_Window))
        {
            ImGui::DestroyContext();
            ResetSwapChainState(false);
            return false;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE fontCpu = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
        const D3D12_GPU_DESCRIPTOR_HANDLE fontGpu = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
        if (!ImGui_ImplDX12_Init(
                m_Device.Get(),
                static_cast<int>(description.BufferCount),
                m_BackBufferFormat,
                m_SrvHeap.Get(),
                fontCpu,
                fontGpu))
        {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            ResetSwapChainState(false);
            return false;
        }

        m_ImGuiReady = true;

        ::SetLastError(0);
        const LONG_PTR previousWindowProc = ::SetWindowLongPtrW(
            m_Window,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&OverlayWindowProc));
        if (previousWindowProc == 0 && ::GetLastError() != 0)
        {
            ResetSwapChainState(false);
            return false;
        }

        m_OriginalWindowProc = reinterpret_cast<WNDPROC>(previousWindowProc);
        m_RenderReady.store(true, std::memory_order_release);
        Core::Logger::Get().Info("render", "GTA DX12/ImGui overlay initialized; F5 toggles menu");
        return true;
    }

    void Renderer::RenderMenuFrame() noexcept
    {
        if (!m_RenderReady.load(std::memory_order_acquire)
            || !m_SwapChain
            || !m_CommandList
            || !m_Fence
            || m_Frames.empty())
        {
            return;
        }

        ID3D12CommandQueue* commandQueue = m_CommandQueue.load();
        if (!commandQueue)
            return;

        const UINT frameIndex = m_SwapChain->GetCurrentBackBufferIndex();
        if (frameIndex >= m_Frames.size())
            return;

        FrameContext& frame = m_Frames[frameIndex];
        if (frame.FenceValue != 0 && m_Fence->GetCompletedValue() < frame.FenceValue)
            return;

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        UI::Menu::Get().Render();
        ImGui::Render();

        ImDrawData* drawData = ImGui::GetDrawData();
        if (!drawData || drawData->TotalVtxCount <= 0)
            return;

        if (FAILED(frame.CommandAllocator->Reset())
            || FAILED(m_CommandList->Reset(frame.CommandAllocator.Get(), nullptr)))
        {
            return;
        }

        D3D12_RESOURCE_BARRIER toRenderTarget{};
        toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRenderTarget.Transition.pResource = frame.BackBuffer.Get();
        toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_CommandList->ResourceBarrier(1, &toRenderTarget);

        m_CommandList->OMSetRenderTargets(1, &frame.Rtv, FALSE, nullptr);
        ID3D12DescriptorHeap* descriptorHeaps[] = {m_SrvHeap.Get()};
        m_CommandList->SetDescriptorHeaps(1, descriptorHeaps);
        ImGui_ImplDX12_RenderDrawData(drawData, m_CommandList.Get());

        D3D12_RESOURCE_BARRIER toPresent = toRenderTarget;
        toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_CommandList->ResourceBarrier(1, &toPresent);

        if (FAILED(m_CommandList->Close()))
            return;

        ID3D12CommandList* commandLists[] = {m_CommandList.Get()};
        commandQueue->ExecuteCommandLists(1, commandLists);

        const std::uint64_t fenceValue = ++m_NextFenceValue;
        if (SUCCEEDED(commandQueue->Signal(m_Fence.Get(), fenceValue)))
            frame.FenceValue = fenceValue;
    }

    void Renderer::ResetSwapChainState(bool restoreWindowProc) noexcept
    {
        m_RenderReady.store(false, std::memory_order_release);

        if (restoreWindowProc && m_Window && m_OriginalWindowProc)
        {
            const auto currentWindowProc = reinterpret_cast<WNDPROC>(::GetWindowLongPtrW(m_Window, GWLP_WNDPROC));
            if (currentWindowProc == &OverlayWindowProc)
            {
                ::SetWindowLongPtrW(
                    m_Window,
                    GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(m_OriginalWindowProc));
            }
        }

        if (m_ImGuiReady)
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            m_ImGuiReady = false;
        }

        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();

        m_Frames.clear();
        m_Fence.Reset();
        m_CommandList.Reset();
        m_SrvHeap.Reset();
        m_RtvHeap.Reset();
        m_Device.Reset();
        m_SwapChain.Reset();

        m_Window = nullptr;
        m_OriginalWindowProc = nullptr;
        m_BackBufferFormat = DXGI_FORMAT_UNKNOWN;
        m_RtvDescriptorSize = 0;
        m_NextFenceValue = 0;
    }
}
