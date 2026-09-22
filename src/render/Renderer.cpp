#include "Renderer.hpp"

#include "../core/Logger.hpp"
#include "../ui/Menu.hpp"
#include "../ui/UtilityOverlay.hpp"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <wincodec.h>
#include <algorithm>
#include <cstring>
#include <vector>

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
            switch (message)
            {
            case WM_MOUSEMOVE:
            case WM_MOUSELEAVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_SETCURSOR:
                return true;
            default:
                return false;
            }
        }

        bool IsKeyboardMessage(UINT message) noexcept
        {
            switch (message)
            {
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
            case WM_DEADCHAR:
            case WM_SYSCHAR:
            case WM_SYSDEADCHAR:
                return true;
            default:
                return false;
            }
        }

        constexpr UINT OverlaySrvDescriptorCount = 128;

        bool DecodeImageFile(
            const std::filesystem::path& path,
            std::vector<std::uint8_t>& pixels,
            UINT& width,
            UINT& height) noexcept
        {
            pixels.clear();
            width = 0;
            height = 0;

            const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool uninitialize = SUCCEEDED(com);
            if (FAILED(com) && com != RPC_E_CHANGED_MODE)
                return false;

            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;

            HRESULT hr = ::CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf()));

            if (SUCCEEDED(hr))
            {
                hr = factory->CreateDecoderFromFilename(
                    path.c_str(),
                    nullptr,
                    GENERIC_READ,
                    WICDecodeMetadataCacheOnLoad,
                    decoder.GetAddressOf());
            }
            if (SUCCEEDED(hr))
                hr = decoder->GetFrame(0, frame.GetAddressOf());
            if (SUCCEEDED(hr))
                hr = factory->CreateFormatConverter(converter.GetAddressOf());
            if (SUCCEEDED(hr))
            {
                hr = converter->Initialize(
                    frame.Get(),
                    GUID_WICPixelFormat32bppRGBA,
                    WICBitmapDitherTypeNone,
                    nullptr,
                    0.0,
                    WICBitmapPaletteTypeCustom);
            }
            if (SUCCEEDED(hr))
                hr = converter->GetSize(&width, &height);

            constexpr UINT MaxDimension = 4096;
            if (SUCCEEDED(hr)
                && (width == 0 || height == 0 || width > MaxDimension || height > MaxDimension))
            {
                hr = E_INVALIDARG;
            }

            const std::size_t pitch = static_cast<std::size_t>(width) * 4u;
            const std::size_t total = pitch * static_cast<std::size_t>(height);
            if (SUCCEEDED(hr) && (total == 0 || total > 128u * 1024u * 1024u))
                hr = E_OUTOFMEMORY;

            if (SUCCEEDED(hr))
            {
                pixels.resize(total);
                hr = converter->CopyPixels(
                    nullptr,
                    static_cast<UINT>(pitch),
                    static_cast<UINT>(total),
                    pixels.data());
            }

            if (uninitialize)
                ::CoUninitialize();

            return SUCCEEDED(hr) && !pixels.empty();
        }

        bool IsPrimaryWindowCandidate(HWND window) noexcept
        {
            if (!window || !::IsWindow(window) || !::IsWindowVisible(window))
                return false;
            if (::GetAncestor(window, GA_ROOT) != window)
                return false;

            DWORD processId{};
            ::GetWindowThreadProcessId(window, &processId);
            if (processId != ::GetCurrentProcessId())
                return false;

            RECT client{};
            if (!::GetClientRect(window, &client))
                return false;

            const LONG width = client.right - client.left;
            const LONG height = client.bottom - client.top;
            return width >= 640 && height >= 360;
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

        Core::Logger::Get().Info("render", "Renderer shell initialized; waiting for GTA primary DX12 swap chain");
        return true;
    }

    void Renderer::Shutdown() noexcept
    {
        if (!m_Initialized.exchange(false))
            return;

        {
            std::scoped_lock lock(m_StateMutex);
            WaitForOverlayIdle();
            ResetSwapChainState();
        }

        ReleasePrimarySelection();
        Core::Logger::Get().Info("render", "Renderer stopped");
    }

    bool Renderer::IsInitialized() const noexcept
    {
        return m_Initialized.load();
    }

    std::uint64_t Renderer::TextureGeneration() const noexcept
    {
        return m_TextureGeneration.load(std::memory_order_acquire);
    }

    bool Renderer::SelectPrimarySwapChain(IDXGISwapChain* swapChain) noexcept
    {
        if (!swapChain)
            return false;

        if (IDXGISwapChain* current = m_PrimarySwapChain.load(std::memory_order_acquire))
            return current == swapChain;

        DXGI_SWAP_CHAIN_DESC description{};
        if (FAILED(swapChain->GetDesc(&description)) || !IsPrimaryWindowCandidate(description.OutputWindow))
            return false;

        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))) || !device)
            return false;

        IDXGISwapChain* expected = nullptr;
        swapChain->AddRef();
        if (!m_PrimarySwapChain.compare_exchange_strong(
                expected,
                swapChain,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            swapChain->Release();
            return expected == swapChain;
        }

        ID3D12Device* retainedDevice = device.Detach();
        m_PrimaryDevice.store(retainedDevice, std::memory_order_release);
        Core::Logger::Get().Info("render", "Primary GTA DX12 swap chain pinned; waiting for matching DIRECT queue");
        return true;
    }

    bool Renderer::QueueMatchesPrimaryDevice(ID3D12CommandQueue* commandQueue) const noexcept
    {
        ID3D12Device* primaryDevice = m_PrimaryDevice.load(std::memory_order_acquire);
        if (!commandQueue || !primaryDevice)
            return false;

        Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
        if (FAILED(commandQueue->GetDevice(IID_PPV_ARGS(&queueDevice))) || !queueDevice)
            return false;

        Microsoft::WRL::ComPtr<IUnknown> primaryIdentity;
        Microsoft::WRL::ComPtr<IUnknown> queueIdentity;
        if (FAILED(primaryDevice->QueryInterface(IID_PPV_ARGS(&primaryIdentity)))
            || FAILED(queueDevice->QueryInterface(IID_PPV_ARGS(&queueIdentity))))
        {
            return false;
        }

        return primaryIdentity.Get() == queueIdentity.Get();
    }

    void Renderer::CaptureCommandQueue(ID3D12CommandQueue* commandQueue) noexcept
    {
        if (!m_Initialized.load(std::memory_order_acquire) || !commandQueue)
            return;

        if (m_CommandQueue.load(std::memory_order_acquire))
            return;

        if (!m_PrimarySwapChain.load(std::memory_order_acquire)
            || !m_PrimaryDevice.load(std::memory_order_acquire))
        {
            return;
        }

        if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
            return;
        if (!QueueMatchesPrimaryDevice(commandQueue))
            return;

        ID3D12CommandQueue* expected = nullptr;
        commandQueue->AddRef();
        if (!m_CommandQueue.compare_exchange_strong(
                expected,
                commandQueue,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            commandQueue->Release();
            return;
        }

        Core::Logger::Get().Info("render", "Captured matching GTA DIRECT command queue after primary swap-chain selection");
    }

    void Renderer::OnPresent(IDXGISwapChain* swapChain) noexcept
    {
        if (!m_Initialized.load(std::memory_order_acquire) || !swapChain)
            return;
        if (!SelectPrimarySwapChain(swapChain))
            return;

        const bool insertDown = (::GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        const bool f4Down = (::GetAsyncKeyState(VK_F4) & 0x8000) != 0;
        const bool toggleKeyDown = insertDown || f4Down;
        const bool wasDown = m_ToggleKeyDown.exchange(toggleKeyDown, std::memory_order_acq_rel);
        if (toggleKeyDown && !wasDown)
        {
            UI::Menu::Get().Toggle();
            const bool open = UI::Menu::Get().IsOpen();

            if (ImGui::GetCurrentContext())
                ImGui::GetIO().MouseDrawCursor = open;
            if (open)
                ::ReleaseCapture();

            Core::Logger::Get().Info(
                "render",
                open ? "Insert/F4 opened V2 menu state" : "Insert/F4 closed V2 menu state");
        }

        if (!UI::Menu::Get().IsOpen() && !UI::UtilityOverlay::AnyVisible())
            return;
        if (!m_CommandQueue.load(std::memory_order_acquire))
            return;

        if (!m_RenderReady.load(std::memory_order_acquire))
        {
            std::scoped_lock lock(m_StateMutex);
            if (!m_RenderReady.load(std::memory_order_relaxed) && !InitializeSwapChain(swapChain))
                return;
        }

        RenderMenuFrame();
    }

    void Renderer::OnBeforeResize(IDXGISwapChain* swapChain) noexcept
    {
        if (!m_Initialized.load(std::memory_order_acquire) || !swapChain)
            return;
        if (m_PrimarySwapChain.load(std::memory_order_acquire) != swapChain)
            return;

        std::scoped_lock lock(m_StateMutex);
        if (m_SwapChain && static_cast<IDXGISwapChain*>(m_SwapChain.Get()) == swapChain)
        {
            Core::Logger::Get().Info("render", "Primary DX12 swap chain resize detected; draining overlay work");
            WaitForOverlayIdle();
            ResetSwapChainState();
        }
    }

    LRESULT Renderer::HandleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
        WNDPROC original = m_OriginalWindowProc;

        if (m_ImGuiReady && UI::Menu::Get().IsOpen())
        {
            if (message == WM_INPUT)
            {
                static_cast<void>(::DefWindowProcW(window, message, wParam, lParam));
                return 0;
            }

            const bool imguiHandled = ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam) != 0;

            if (IsMouseMessage(message) || IsKeyboardMessage(message) || imguiHandled)
                return 1;
        }

        if (original)
            return ::CallWindowProcW(original, window, message, wParam, lParam);

        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    bool Renderer::InitializeSwapChain(IDXGISwapChain* swapChain) noexcept
    {
        if (m_PrimarySwapChain.load(std::memory_order_acquire) != swapChain)
            return false;

        ID3D12CommandQueue* commandQueue = m_CommandQueue.load(std::memory_order_acquire);
        if (!commandQueue || !QueueMatchesPrimaryDevice(commandQueue))
            return false;

        Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
        if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))))
            return false;

        DXGI_SWAP_CHAIN_DESC description{};
        if (FAILED(swapChain3->GetDesc(&description))
            || !IsPrimaryWindowCandidate(description.OutputWindow)
            || description.BufferCount == 0)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(swapChain3->GetDevice(IID_PPV_ARGS(&device))) || !device)
            return false;

        m_SwapChain = swapChain3;
        m_Device = device;
        m_Window = description.OutputWindow;
        m_BackBufferFormat = description.BufferDesc.Format == DXGI_FORMAT_UNKNOWN
            ? DXGI_FORMAT_R8G8B8A8_UNORM
            : description.BufferDesc.Format;

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
        rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDescription.NumDescriptors = description.BufferCount;
        if (FAILED(m_Device->CreateDescriptorHeap(&rtvHeapDescription, IID_PPV_ARGS(&m_RtvHeap))))
        {
            ResetSwapChainState();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDescription{};
        srvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDescription.NumDescriptors = OverlaySrvDescriptorCount;
        srvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_Device->CreateDescriptorHeap(&srvHeapDescription, IID_PPV_ARGS(&m_SrvHeap))))
        {
            ResetSwapChainState();
            return false;
        }

        m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_SrvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_SrvSlotsUsed.assign(OverlaySrvDescriptorCount, false);
        m_SrvSlotsUsed[0] = true;
        m_RetiredTextures.clear();
        m_Frames.clear();
        m_Frames.resize(description.BufferCount);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < description.BufferCount; ++index)
        {
            FrameContext& frame = m_Frames[index];
            if (FAILED(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.CommandAllocator)))
                || FAILED(m_SwapChain->GetBuffer(index, IID_PPV_ARGS(&frame.BackBuffer))))
            {
                ResetSwapChainState();
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
            ResetSwapChainState();
            return false;
        }
        if (FAILED(m_CommandList->Close()))
        {
            ResetSwapChainState();
            return false;
        }

        if (FAILED(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence))))
        {
            ResetSwapChainState();
            return false;
        }

        m_FenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_FenceEvent)
        {
            ResetSwapChainState();
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.MouseDrawCursor = UI::Menu::Get().IsOpen();
        ImGui::StyleColorsDark();

        if (!ImGui_ImplWin32_Init(m_Window))
        {
            ImGui::DestroyContext();
            ResetSwapChainState();
            return false;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE fontCpu = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
        const D3D12_GPU_DESCRIPTOR_HANDLE fontGpu = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();

        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.Device = m_Device.Get();
        initInfo.CommandQueue = commandQueue;
        initInfo.NumFramesInFlight = static_cast<int>(description.BufferCount);
        initInfo.RTVFormat = m_BackBufferFormat;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.SrvDescriptorHeap = m_SrvHeap.Get();
        initInfo.LegacySingleSrvCpuDescriptor = fontCpu;
        initInfo.LegacySingleSrvGpuDescriptor = fontGpu;

        if (!ImGui_ImplDX12_Init(&initInfo))
        {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            ResetSwapChainState();
            return false;
        }

        m_ImGuiReady = true;
        m_TextureGeneration.fetch_add(1, std::memory_order_acq_rel);
        if (!AttachInputHook())
        {
            ResetSwapChainState();
            return false;
        }

        m_RenderReady.store(true, std::memory_order_release);
        Core::Logger::Get().Info(
            "render",
            "GTA DX12/ImGui overlay initialized with captured DIRECT queue and menu input capture");
        return true;
    }

    bool Renderer::AttachInputHook() noexcept
    {
        if (m_InputHooked.load(std::memory_order_acquire))
            return true;
        if (!m_Window || !::IsWindow(m_Window))
            return false;

        ::SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous = ::SetWindowLongPtrW(
            m_Window,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&OverlayWindowProc));
        const DWORD error = ::GetLastError();

        if (previous == 0)
        {
            if (error != ERROR_SUCCESS)
                Core::Logger::Get().Error("render.input", "Failed to install GTA window input hook");
            else
                Core::Logger::Get().Error("render.input", "GTA window input hook returned no original WndProc");
            return false;
        }

        m_OriginalWindowProc = reinterpret_cast<WNDPROC>(previous);
        m_InputHooked.store(true, std::memory_order_release);
        Core::Logger::Get().Info("render.input", "Validated GTA WndProc input hook installed");
        return true;
    }

    void Renderer::DetachInputHook() noexcept
    {
        if (!m_InputHooked.exchange(false, std::memory_order_acq_rel))
            return;

        if (m_Window && m_OriginalWindowProc && ::IsWindow(m_Window))
        {
            ::SetLastError(ERROR_SUCCESS);
            const LONG_PTR result = ::SetWindowLongPtrW(
                m_Window,
                GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(m_OriginalWindowProc));
            if (result == 0 && ::GetLastError() != ERROR_SUCCESS)
                Core::Logger::Get().Error("render.input", "Failed to restore GTA original WndProc");
            else
                Core::Logger::Get().Info("render.input", "GTA original WndProc restored");
        }

        m_OriginalWindowProc = nullptr;
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

        ID3D12CommandQueue* commandQueue = m_CommandQueue.load(std::memory_order_acquire);
        if (!commandQueue)
            return;

        const UINT frameIndex = m_SwapChain->GetCurrentBackBufferIndex();
        if (frameIndex >= m_Frames.size())
            return;

        FrameContext& frame = m_Frames[frameIndex];
        if (frame.FenceValue != 0 && m_Fence->GetCompletedValue() < frame.FenceValue)
            return;

        CollectTextureGarbage();

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        UI::Menu::Get().Render();
        UI::UtilityOverlay::Render();
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

    bool Renderer::LoadTextureFile(const std::filesystem::path& path, TextureHandle& out) noexcept
    {
        out = {};

        std::vector<std::uint8_t> pixels;
        UINT width{};
        UINT height{};
        if (!DecodeImageFile(path, pixels, width, height))
            return false;

        std::scoped_lock lock(m_StateMutex);
        if (!m_RenderReady.load(std::memory_order_acquire)
            || !m_Device
            || !m_SrvHeap
            || !m_Fence
            || !m_FenceEvent)
        {
            return false;
        }

        ID3D12CommandQueue* queue = m_CommandQueue.load(std::memory_order_acquire);
        if (!queue)
            return false;

        CollectTextureGarbage();

        UINT slot = UINT_MAX;
        for (UINT index = 1; index < static_cast<UINT>(m_SrvSlotsUsed.size()); ++index)
        {
            if (!m_SrvSlotsUsed[index])
            {
                slot = index;
                break;
            }
        }
        if (slot == UINT_MAX)
            return false;

        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        if (FAILED(m_Device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(texture.GetAddressOf()))))
        {
            return false;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows{};
        UINT64 rowBytes{};
        UINT64 uploadSize{};
        m_Device->GetCopyableFootprints(
            &textureDesc,
            0,
            1,
            0,
            &footprint,
            &rows,
            &rowBytes,
            &uploadSize);

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC uploadDesc{};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width = uploadSize;
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> upload;
        if (FAILED(m_Device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(upload.GetAddressOf()))))
        {
            return false;
        }

        std::uint8_t* mapped{};
        D3D12_RANGE noRead{0, 0};
        if (FAILED(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped))) || !mapped)
            return false;

        const std::size_t sourcePitch = static_cast<std::size_t>(width) * 4u;
        for (UINT row = 0; row < height; ++row)
        {
            std::memcpy(
                mapped + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                pixels.data() + static_cast<std::size_t>(row) * sourcePitch,
                sourcePitch);
        }
        upload->Unmap(0, nullptr);

        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
        if (FAILED(m_Device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(allocator.GetAddressOf())))
            || FAILED(m_Device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(list.GetAddressOf()))))
        {
            return false;
        }

        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;

        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);

        if (FAILED(list->Close()))
            return false;

        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);

        const std::uint64_t uploadFence = ++m_NextFenceValue;
        if (FAILED(queue->Signal(m_Fence.Get(), uploadFence)))
            return false;

        if (m_Fence->GetCompletedValue() < uploadFence)
        {
            if (FAILED(m_Fence->SetEventOnCompletion(uploadFence, m_FenceEvent)))
                return false;
            static_cast<void>(::WaitForSingleObject(m_FenceEvent, INFINITE));
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = textureDesc.Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;

        auto cpu = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(slot) * m_SrvDescriptorSize;
        auto gpu = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(slot) * m_SrvDescriptorSize;

        m_Device->CreateShaderResourceView(texture.Get(), &srv, cpu);
        m_SrvSlotsUsed[slot] = true;

        out.Resource = std::move(texture);
        out.TextureId = static_cast<std::uint64_t>(gpu.ptr);
        out.Width = width;
        out.Height = height;
        out.DescriptorIndex = slot;
        return true;
    }

    void Renderer::ReleaseTexture(TextureHandle& texture) noexcept
    {
        if (!texture.Valid())
        {
            texture = {};
            return;
        }

        std::scoped_lock lock(m_StateMutex);

        if (!m_SrvHeap
            || texture.DescriptorIndex >= m_SrvSlotsUsed.size()
            || !m_Fence)
        {
            texture = {};
            return;
        }

        m_RetiredTextures.push_back(RetiredTexture{
            texture.Resource,
            texture.DescriptorIndex,
            m_NextFenceValue,
        });
        texture = {};
        CollectTextureGarbage();
    }

    void Renderer::CollectTextureGarbage() noexcept
    {
        if (!m_Fence)
            return;

        const std::uint64_t completed = m_Fence->GetCompletedValue();
        for (auto it = m_RetiredTextures.begin(); it != m_RetiredTextures.end();)
        {
            if (it->FenceValue > completed)
            {
                ++it;
                continue;
            }

            if (it->DescriptorIndex < m_SrvSlotsUsed.size())
                m_SrvSlotsUsed[it->DescriptorIndex] = false;
            it = m_RetiredTextures.erase(it);
        }
    }

    void Renderer::WaitForOverlayIdle() noexcept
    {
        ID3D12CommandQueue* commandQueue = m_CommandQueue.load(std::memory_order_acquire);
        if (!commandQueue || !m_Fence || !m_FenceEvent)
            return;

        const std::uint64_t fenceValue = ++m_NextFenceValue;
        if (FAILED(commandQueue->Signal(m_Fence.Get(), fenceValue)))
            return;
        if (m_Fence->GetCompletedValue() >= fenceValue)
            return;
        if (FAILED(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent)))
            return;

        static_cast<void>(::WaitForSingleObject(m_FenceEvent, INFINITE));
    }

    void Renderer::ResetSwapChainState() noexcept
    {
        m_RenderReady.store(false, std::memory_order_release);
        DetachInputHook();

        if (m_ImGuiReady)
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            m_ImGuiReady = false;
        }

        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();

        m_RetiredTextures.clear();
        m_SrvSlotsUsed.clear();
        m_SrvDescriptorSize = 0;
        m_Frames.clear();
        m_Fence.Reset();
        m_CommandList.Reset();
        m_SrvHeap.Reset();
        m_RtvHeap.Reset();
        m_Device.Reset();
        m_SwapChain.Reset();

        if (m_FenceEvent)
        {
            ::CloseHandle(m_FenceEvent);
            m_FenceEvent = nullptr;
        }

        m_Window = nullptr;
        m_BackBufferFormat = DXGI_FORMAT_UNKNOWN;
        m_RtvDescriptorSize = 0;
        m_NextFenceValue = 0;
    }

    void Renderer::ReleasePrimarySelection() noexcept
    {
        if (ID3D12CommandQueue* commandQueue = m_CommandQueue.exchange(nullptr, std::memory_order_acq_rel))
            commandQueue->Release();
        if (ID3D12Device* device = m_PrimaryDevice.exchange(nullptr, std::memory_order_acq_rel))
            device->Release();
        if (IDXGISwapChain* swapChain = m_PrimarySwapChain.exchange(nullptr, std::memory_order_acq_rel))
            swapChain->Release();

        m_ToggleKeyDown.store(false, std::memory_order_release);
    }
}
