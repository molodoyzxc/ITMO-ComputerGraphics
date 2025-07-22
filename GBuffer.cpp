// GBuffer.cpp
#include "GBuffer.h"
#include "d3dx12.h"
#include <stdexcept>

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

GBuffer::GBuffer(DX12Framework* framework,
    UINT width, UINT height,
    ID3D12DescriptorHeap* rtvHeap, UINT rtvSize,
    ID3D12DescriptorHeap* srvHeap, UINT srvSize)
    : m_framework(framework)
    , m_width(width), m_height(height)
    , m_rtvHeap(rtvHeap), m_rtvDescriptorSize(rtvSize)
    , m_srvHeap(srvHeap), m_srvDescriptorSize(srvSize)
{
    // резервируем слоты; допустим, они подряд
    m_rtvStartIndex = 200;
    m_srvStartIndex = 200;
}

void GBuffer::Initialize() {
    CreateResources();
    CreateDescriptors();
}

void GBuffer::CreateResources() {
    // общий описатель для render‑target
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height,
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
    );
    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE clearRT = {};
    clearRT.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearRT.Color[0] = 0; clearRT.Color[1] = 0;
    clearRT.Color[2] = 0; clearRT.Color[3] = 0;

    // албедо
    ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearRT, IID_PPV_ARGS(&m_rtAlbedo)));

    // нормали (16‑bit float + RT flag)
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clearRT.Format = texDesc.Format;
    ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearRT, IID_PPV_ARGS(&m_rtNormal)));

    // параметры материала
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearRT.Format = texDesc.Format;
    ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearRT, IID_PPV_ARGS(&m_rtMaterial)));

    // depth‑buffer
    CD3DX12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, m_width, m_height,
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    );
    D3D12_CLEAR_VALUE clearDS = {};
    clearDS.Format = DXGI_FORMAT_D32_FLOAT;
    clearDS.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearDS, IID_PPV_ARGS(&m_depth)));
}

void GBuffer::CreateDescriptors() {
    // RTV: албедо, нормали, материал
    auto rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_rtvStartIndex, m_rtvDescriptorSize);
    m_framework->GetDevice()->CreateRenderTargetView(m_rtAlbedo.Get(), nullptr, rtvHandle);

    rtvHandle.Offset(1, m_rtvDescriptorSize);
    m_framework->GetDevice()->CreateRenderTargetView(m_rtNormal.Get(), nullptr, rtvHandle);

    rtvHandle.Offset(1, m_rtvDescriptorSize);
    m_framework->GetDevice()->CreateRenderTargetView(m_rtMaterial.Get(), nullptr, rtvHandle);

    // DSV для depth
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_framework->GetDSVHandle());
    m_framework->GetDevice()->CreateDepthStencilView(m_depth.Get(), nullptr, dsvHandle);

    // SRV: для передачи в пасс освещения
    auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvStartIndex, m_srvDescriptorSize);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    m_framework->GetDevice()->CreateShaderResourceView(m_rtAlbedo.Get(), &srvDesc, srvHandle);

    srvHandle.Offset(1, m_srvDescriptorSize);
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    m_framework->GetDevice()->CreateShaderResourceView(m_rtNormal.Get(), &srvDesc, srvHandle);

    srvHandle.Offset(1, m_srvDescriptorSize);
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_framework->GetDevice()->CreateShaderResourceView(m_rtMaterial.Get(), &srvDesc, srvHandle);
}

void GBuffer::Bind(ID3D12GraphicsCommandList* cmd) {
    // переходы в RT/DV
    D3D12_RESOURCE_BARRIER barriers[4] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_rtAlbedo.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(m_rtNormal.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(m_rtMaterial.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(m_depth.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE)
    };
    cmd->ResourceBarrier(4, barriers);

    // привязать MRT
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
        CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_rtvStartIndex, m_rtvDescriptorSize),
        CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_rtvStartIndex + 1, m_rtvDescriptorSize),
        CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_rtvStartIndex + 2, m_rtvDescriptorSize)
    };
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_framework->GetDSVHandle());
    cmd->OMSetRenderTargets(_countof(rtvs), rtvs, FALSE, &dsv);
}

void GBuffer::Clear(ID3D12GraphicsCommandList* cmd, const FLOAT clearColor[4]) {
    // очистка каждого RTV
    for (UINT i = 0; i < 3; ++i) {
        auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_rtvStartIndex + i, m_rtvDescriptorSize);
        cmd->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    }
    // очистка depth
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_framework->GetDSVHandle());
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> GBuffer::GetSRVs() const {
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> handles(3);
    auto gpuStart = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        m_srvHeap->GetGPUDescriptorHandleForHeapStart(),
        m_srvStartIndex, m_srvDescriptorSize);
    for (int i = 0; i < 3; ++i)
        handles[i] = CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, i, m_srvDescriptorSize);
    return handles;
}

void GBuffer::TransitionToReadable(ID3D12GraphicsCommandList* cmd) {
    // Transition back to shader resource
    D3D12_RESOURCE_BARRIER barriers[4] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_rtAlbedo.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_rtNormal.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_rtMaterial.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_depth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };
    cmd->ResourceBarrier(4, barriers);
}