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

    m_rtvStartIndex = 2;
    m_srvStartIndex = m_framework->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4);
    m_srvNormalIndex = m_srvStartIndex + 1;
    m_srvMaterialIndex = m_srvStartIndex + 2;
    depthSrvIndex = m_srvStartIndex + 3;
}

void GBuffer::Initialize() {
    CreateResources();
    CreateDescriptors();
}

void GBuffer::CreateResources() {
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height,
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
    );
    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE clearRT = {};
    clearRT.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearRT.Color[0] = 0.2f; clearRT.Color[1] = 0.2f;
    clearRT.Color[2] = 1.0f; clearRT.Color[3] = 1.0f;

    ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearRT, IID_PPV_ARGS(&m_rtAlbedo)));

    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clearRT.Format = texDesc.Format;
    ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearRT, IID_PPV_ARGS(&m_rtNormal)));

    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearRT.Format = texDesc.Format;
    ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearRT, IID_PPV_ARGS(&m_rtMaterial)));

    CD3DX12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_TYPELESS,
        m_width, m_height,
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    );
    D3D12_CLEAR_VALUE clearDS = {};
    clearDS.Format = DXGI_FORMAT_D32_FLOAT;
    clearDS.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearDS, IID_PPV_ARGS(&m_depth))); 
}

void GBuffer::CreateDescriptors() {
    auto cpuRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_rtvStartIndex, m_rtvDescriptorSize);
    m_framework->GetDevice()->CreateRenderTargetView(m_rtAlbedo.Get(), nullptr, cpuRTV);

    cpuRTV.Offset(1, m_rtvDescriptorSize);
    m_framework->GetDevice()->CreateRenderTargetView(m_rtNormal.Get(), nullptr, cpuRTV);

    cpuRTV.Offset(1, m_rtvDescriptorSize);
    m_framework->GetDevice()->CreateRenderTargetView(m_rtMaterial.Get(), nullptr, cpuRTV);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.Texture2D.MipSlice = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_framework->GetDSVHandle());
    m_framework->GetDevice()->CreateDepthStencilView(m_depth.Get(), &dsvDesc, dsvHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    D3D12_SHADER_RESOURCE_VIEW_DESC dsDesc = {};
    dsDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    dsDesc.Format = DXGI_FORMAT_R32_FLOAT;
    dsDesc.Texture2D.MipLevels = 1;
    dsDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    auto cpuSRV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvStartIndex, m_srvDescriptorSize);
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_framework->GetDevice()->CreateShaderResourceView(m_rtAlbedo.Get(), &srvDesc, cpuSRV);

    cpuSRV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvNormalIndex, m_srvDescriptorSize);
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    m_framework->GetDevice()->CreateShaderResourceView(m_rtNormal.Get(), &srvDesc, cpuSRV);

    cpuSRV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvMaterialIndex, m_srvDescriptorSize);
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_framework->GetDevice()->CreateShaderResourceView(m_rtMaterial.Get(), &srvDesc, cpuSRV);

    auto cpuDS = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        depthSrvIndex, m_srvDescriptorSize);
    m_framework->GetDevice()->CreateShaderResourceView(m_depth.Get(), &dsDesc, cpuDS);
}

void GBuffer::Bind(ID3D12GraphicsCommandList* cmd) {
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
    for (UINT i = 0; i < 3; ++i) {
        auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_rtvStartIndex + i, m_rtvDescriptorSize);
        cmd->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_framework->GetDSVHandle());
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> GBuffer::GetSRVs() const {
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> handles(4);
    auto gpuStart = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        m_srvHeap->GetGPUDescriptorHandleForHeapStart(),
        m_srvStartIndex, m_srvDescriptorSize);
    for (int i = 0; i < 4; ++i)
        handles[i] = CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, i, m_srvDescriptorSize);
    return handles;
}

void GBuffer::TransitionToReadable(ID3D12GraphicsCommandList* cmd) {
    D3D12_RESOURCE_BARRIER barriers[4] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_rtAlbedo.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_rtNormal.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_rtMaterial.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_depth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };
    cmd->ResourceBarrier(4, barriers);
}