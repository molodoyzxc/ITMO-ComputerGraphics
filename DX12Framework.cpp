#include "DX12Framework.h"
#include <dxgi1_4.h>
#include "d3dx12.h"
#include <stdexcept>
#include "WICTextureLoader.h"
using Microsoft::WRL::ComPtr;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

DX12Framework::DX12Framework(HWND hwnd, UINT width, UINT height)
    : m_hwnd(hwnd)
    , m_width(width)
    , m_height(height)
    , m_backBufferIndex(0)
    , m_fenceValue(0)
{
}

DX12Framework::~DX12Framework()
{
    WaitForGpu();
    CloseHandle(m_fenceEvent);
}

void DX12Framework::Init()
{
    CreateDevice();             // устройство
    CreateCommandObjects();     // очередь, allocator, список команд, fence
    CreateSwapChain();          // свапчейн
    CreateDescriptorHeaps();    // кучи дескрипторов RTV/DSV
    CreateRenderTargetViews();  // RTV для бэкбуферов
    CreateDepthResources();     // буфер глубины
    BuildDefaultResources();
}

// ID3D12Device
void DX12Framework::CreateDevice()
{
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    ThrowIfFailed(D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&m_device)));

    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(m_device.As(&infoQueue))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        D3D12_MESSAGE_ID denyIds[] = { D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = _countof(denyIds);
        filter.DenyList.pIDList = denyIds;
        infoQueue->AddStorageFilterEntries(&filter);
    }
}

// очередь, allocator, список команд, fence
void DX12Framework::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC cqDesc = {};
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&cqDesc,
        IID_PPV_ARGS(&m_commandQueue)));

    ThrowIfFailed(m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_commandAllocator)));

    ThrowIfFailed(m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator.Get(), nullptr,
        IID_PPV_ARGS(&m_commandList)));

    m_commandList->Close();

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&m_fence)));

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

// swap chain
void DX12Framework::CreateSwapChain()
{
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.BufferCount = FrameCount;
    sd.Width = m_width;
    sd.Height = m_height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(), m_hwnd, &sd,
        nullptr, nullptr, &swapChain1));

    ThrowIfFailed(swapChain1.As(&m_swapChain));

    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// RTV и DSV дескрипторные кучи.
void DX12Framework::CreateDescriptorHeaps()
{
    // RTV куча
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = FrameCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(
        &rtvDesc, IID_PPV_ARGS(&m_rtvHeap)));

    m_rtvDescriptorSize =
        m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // DSV куча
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(
        &dsvDesc, IID_PPV_ARGS(&m_dsvHeap)));

    m_dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 100;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)));
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC sampDesc = {};
    sampDesc.NumDescriptors = 1;
    sampDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&sampDesc, IID_PPV_ARGS(&m_samplerHeap)));
    m_samplerDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MipLODBias = 0;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplerDesc.BorderColor[0] = samplerDesc.BorderColor[1]
        = samplerDesc.BorderColor[2]
        = samplerDesc.BorderColor[3] = 0.0f;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

    CD3DX12_CPU_DESCRIPTOR_HANDLE sampHandleCPU(
        m_samplerHeap->GetCPUDescriptorHandleForHeapStart()
    );

    m_device->CreateSampler(&samplerDesc, sampHandleCPU);
}

void DX12Framework::CreateRenderTargetViews()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < FrameCount; ++i)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(
            i, IID_PPV_ARGS(&m_renderTargets[i])));

        m_device->CreateRenderTargetView(
            m_renderTargets[i].Get(), nullptr, rtvHandle);

        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
}

// 2D текстура глубины
void DX12Framework::CreateDepthResources()
{
    CD3DX12_RESOURCE_DESC depthDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT,
            m_width, m_height,
            1, 1,
            1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE clearVal{};
    clearVal.Format = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth = 1.0f;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearVal,
        IID_PPV_ARGS(&m_depthBuffer)));

    m_device->CreateDepthStencilView(
        m_depthBuffer.Get(), nullptr, m_dsvHandle);
}

void DX12Framework::Present()
{
    ThrowIfFailed(m_swapChain->Present(1, 0));
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// fence
void DX12Framework::WaitForGpu()
{
    const UINT64 fence = ++m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));

    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(
            fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void DX12Framework::ClearColorAndDepthBuffer(float clear[4])
{
    auto rtv = GetCurrentRTVHandle();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &m_dsvHandle);
    m_commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);
    m_commandList->ClearDepthStencilView(m_dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void DX12Framework::SetViewportAndScissors()
{
    D3D12_VIEWPORT vp{ 0,0,m_width,m_height,0.0f,1.0f };
    D3D12_RECT sr{ 0,0,m_width,m_height };
    m_commandList->RSSetViewports(1, &vp);
    m_commandList->RSSetScissorRects(1, &sr);
}

void DX12Framework::SetRootSignatureAndPSO(ID3D12RootSignature* root, ID3D12PipelineState* state)
{
    m_commandList->SetGraphicsRootSignature(root);
    m_commandList->SetPipelineState(state);
}

// ClearRenderTargetView и ClearDepthStencilView.
void DX12Framework::Clear(const FLOAT clearColor[4])
{
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(
        m_commandAllocator.Get(), nullptr));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_backBufferIndex, m_rtvDescriptorSize);

    m_commandList->OMSetRenderTargets(1, &rtv, FALSE,
        &m_dsvHandle);

    m_commandList->ClearRenderTargetView(
        rtv, clearColor, 0, nullptr);

    m_commandList->ClearDepthStencilView(
        m_dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0, 0, nullptr);

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* lists[]{ m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    WaitForGpu();
}

void DX12Framework::BeginFrame()
{
    auto backBuffer = GetCurrentBackBufferResource();
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    m_commandList->ResourceBarrier(1, &barrier);

    auto rtv = GetCurrentRTVHandle();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &m_dsvHandle);
}

void DX12Framework::EndFrame()
{
    auto backBuffer = GetCurrentBackBufferResource();
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT
    );
    m_commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(lists), lists);

    ThrowIfFailed(m_swapChain->Present(1, 0));
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

    const UINT64 fenceToWaitFor = ++m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceToWaitFor));
    if (m_fence->GetCompletedValue() < fenceToWaitFor) {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fenceToWaitFor, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}


void DX12Framework::BuildDefaultResources()
{
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

    // 1×1 текстура
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    texDesc.DepthOrArraySize = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    {
        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(m_device->CreateCommittedResource(
            &heapDefault,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_whiteTexture)
        ));
    }

    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT numRows;
    unsigned long long rowSize;
    m_device->GetCopyableFootprints(
        &texDesc,
        0,
        1,
        0,
        &layout,
        &numRows,
        &rowSize,
        &uploadSize
    );

    {
        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        ThrowIfFailed(m_device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_whiteUploadBuffer)
        ));
    }

    {
        UINT8 whitePixel[4] = { 255, 255, 255, 255 };
        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = whitePixel;
        subData.RowPitch = 4;
        subData.SlicePitch = 4;

        UpdateSubresources(m_commandList.Get(), m_whiteTexture.Get(), m_whiteUploadBuffer.Get(), 0, 0, 1, &subData);

        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_whiteTexture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        m_commandList->ResourceBarrier(1, &barrier);
    }

    ThrowIfFailed(m_commandList->Close());
    {
        ID3D12CommandList* lists[] = { m_commandList.Get()};
        m_commandQueue->ExecuteCommandLists(_countof(lists), lists);
    }
    WaitForGpu();

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        m_whiteSrvIndex = AllocateSrvDescriptor();
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(
            m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_whiteSrvIndex,
            m_srvDescriptorSize
        );
        m_device->CreateShaderResourceView(m_whiteTexture.Get(), &srvDesc, cpuHandle);
    }
}