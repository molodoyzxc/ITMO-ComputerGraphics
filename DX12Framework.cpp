#include "DX12Framework.h"
#include <dxgi1_4.h>
#include "d3dx12.h"
#include <stdexcept>
#include "WICTextureLoader.h"
#include <dxgi1_6.h>
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
}

// ID3D12Device
void DX12Framework::CreateDevice()
{
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) 
    {
        debugController->EnableDebugLayer();
    }

    ComPtr<IDXGIFactory6> factory6;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory6));
    if (FAILED(hr)) 
    {
        ComPtr<IDXGIFactory4> factory4;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)));
        factory4.As(&factory6);
    }

    ComPtr<IDXGIAdapter1> bestAdapter;
    DXGI_ADAPTER_DESC1 bestDesc = {};
    auto tryPickWithPreference = [&]() -> bool {
        if (!factory6) return false;
        for (UINT i = 0;; ++i)
        {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory6->EnumAdapterByGpuPreference(
                i,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) break;

            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                _uuidof(ID3D12Device), nullptr)))
            {
                bestAdapter = adapter;
                bestDesc = desc;
                return true;
            }
        }
        return false;
        };

    auto pickByEnumerateAll = [&]() {
        ComPtr<IDXGIFactory1> factory1;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory1)));
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory1->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;

            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                _uuidof(ID3D12Device), nullptr)))
            {
                if (!bestAdapter || desc.DedicatedVideoMemory > bestDesc.DedicatedVideoMemory) {
                    bestAdapter = adapter;
                    bestDesc = desc;
                }
            }
        }
        };

    bool picked = tryPickWithPreference();
    if (!picked) pickByEnumerateAll();

    if (!bestAdapter) 
    {
        ComPtr<IDXGIFactory4> factory4;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)));
        ThrowIfFailed(factory4->EnumWarpAdapter(IID_PPV_ARGS(&bestAdapter)));
        bestAdapter->GetDesc1(&bestDesc);
    }

    ThrowIfFailed(D3D12CreateDevice(
        bestAdapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&m_device)));

    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(m_device.As(&infoQueue))) 
    {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        D3D12_MESSAGE_ID denyIds[] = { D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE };
        D3D12_INFO_QUEUE_FILTER filter{};
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

// дескрипторные кучи.
void DX12Framework::CreateDescriptorHeaps()
{
    // RTV куча
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = FrameCount + 4;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(
        &rtvDesc, IID_PPV_ARGS(&m_rtvHeap)));

    m_rtvDescriptorSize =
        m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // DSV куча
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.NumDescriptors = 5;
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

    D3D12_DESCRIPTOR_HEAP_DESC samplerDesc = {};
    samplerDesc.NumDescriptors = 2;
    samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(&m_samplerHeap)));
    m_samplerDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    D3D12_SAMPLER_DESC linearSamp = {};
    linearSamp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linearSamp.AddressU = linearSamp.AddressV = linearSamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    linearSamp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    linearSamp.MaxLOD = D3D12_FLOAT32_MAX;
    m_device->CreateSampler(&linearSamp, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SAMPLER_DESC comparisonSamp = {};
    comparisonSamp.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    comparisonSamp.AddressU = comparisonSamp.AddressV = comparisonSamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    comparisonSamp.BorderColor[0] = comparisonSamp.BorderColor[1] = comparisonSamp.BorderColor[2] = comparisonSamp.BorderColor[3] = 0.0f;
    comparisonSamp.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    comparisonSamp.MaxLOD = 0.0f;
    m_device->CreateSampler(&comparisonSamp, CD3DX12_CPU_DESCRIPTOR_HANDLE(m_samplerHeap->GetCPUDescriptorHandleForHeapStart(), 1, m_samplerDescriptorSize));
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

    //ThrowIfFailed(m_swapChain->Present(0, 0)); //vsync off
    ThrowIfFailed(m_swapChain->Present(1, 0)); //vsync on
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

    const UINT64 fenceToWaitFor = ++m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceToWaitFor));
    if (m_fence->GetCompletedValue() < fenceToWaitFor) {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fenceToWaitFor, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

UINT DX12Framework::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count) {
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
         throw std::runtime_error("AllocateDescriptors: unsupported heap type");
    UINT heapSize = m_srvHeap->GetDesc().NumDescriptors;
    if (m_nextSrvDescriptor + count > heapSize)
         throw std::runtime_error("AllocateDescriptors: not enough descriptors");
    UINT start = m_nextSrvDescriptor;
    m_nextSrvDescriptor += count;
    return start; 
}

void DX12Framework::CreateDefaultBuffer(
    ID3D12GraphicsCommandList* cmdList,
    void* initData,
    UINT64 byteSize,
    ComPtr<ID3D12Resource>& defaultBuffer,
    ComPtr<ID3D12Resource>& uploadBuffer)
{
    auto device = GetDevice();

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&defaultBuffer)));

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    subResourceData.RowPitch = byteSize;
    subResourceData.SlicePitch = subResourceData.RowPitch;

    UpdateSubresources<1>(
        cmdList,
        defaultBuffer.Get(),
        uploadBuffer.Get(),
        0, 0, 1,
        &subResourceData);

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);

    cmdList->ResourceBarrier(1, &barrier);
}