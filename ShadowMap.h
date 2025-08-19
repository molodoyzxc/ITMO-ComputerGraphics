#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include "d3dx12.h"
#include "DX12Framework.h"

using Microsoft::WRL::ComPtr;

class ShadowMap 
{
public:
    ShadowMap(DX12Framework* fw, UINT size)
        : m_fw(fw), m_size(size) 
    {
    }

    void Initialize() 
    {
        CreateResource();
        CreateDescriptors();
        m_tex->SetName(L"ShadowMap.Depth");
    }

    D3D12_VIEWPORT GetViewport() const { return { 0.0f,0.0f,(float)m_size,(float)m_size,0.0f,1.0f }; }
    D3D12_RECT     GetScissor()  const { return { 0,0,(LONG)m_size,(LONG)m_size }; }

    D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const { return m_dsvHandle; }
    D3D12_GPU_DESCRIPTOR_HANDLE Srv() const { return m_srvHandleGPU; }
    UINT                        SrvIndex() const { return m_srvIndex; }

    ID3D12Resource* Resource() const { return m_tex.Get(); }
    float Size() const { return static_cast<float>(m_size); }

private:
    static inline void ThrowIfFailed(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("HRESULT failed"); }

    DX12Framework* m_fw;
    UINT m_size;

    ComPtr<ID3D12Resource> m_tex;
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvHandleGPU{};
    UINT m_srvIndex = 0;

    void CreateResource()
    {
        CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS, m_size, m_size, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        );
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0f;

        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(m_fw->GetDevice()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clear, IID_PPV_ARGS(&m_tex)));
    }

    void CreateDescriptors()
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        auto dsvSize = m_fw->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        m_dsvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_fw->GetDSVHandle(), 1, dsvSize);

        m_fw->GetDevice()->CreateDepthStencilView(m_tex.Get(), &dsv, m_dsvHandle);

        m_srvIndex = m_fw->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        auto srvCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_fw->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
            m_srvIndex, m_fw->GetSrvDescriptorSize());
        m_fw->GetDevice()->CreateShaderResourceView(m_tex.Get(), &srv, srvCPU);

        m_srvHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            m_fw->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
            m_srvIndex, m_fw->GetSrvDescriptorSize());
    }
};
