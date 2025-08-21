#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include "d3dx12.h"
#include "DX12Framework.h"
#include <vector>

using Microsoft::WRL::ComPtr;

class ShadowMap
{
public:
    ShadowMap(DX12Framework* fw, UINT size, UINT cascades)
        : m_fw(fw), m_size(size), m_cascades(cascades) 
    {}

    void Initialize()
    {
        CreateResource();
        CreateDescriptors();
        m_tex->SetName(L"ShadowMap.Array");
    }

    D3D12_VIEWPORT GetViewport() const { return { 0.0f,0.0f,(float)m_size,(float)m_size,0.0f,1.0f }; }
    D3D12_RECT     GetScissor()  const { return { 0,0,(LONG)m_size,(LONG)m_size }; }

    D3D12_CPU_DESCRIPTOR_HANDLE Dsv(UINT slice) const { return m_dsvHandles[slice]; }
    D3D12_GPU_DESCRIPTOR_HANDLE Srv() const { return m_srvHandleGPU; }
    UINT                        SrvIndex() const { return m_srvIndex; }

    ID3D12Resource* Resource() const { return m_tex.Get(); }
    float Size() const { return static_cast<float>(m_size); }
    UINT  CascadeCount() const { return m_cascades; }

private:
    static inline void ThrowIfFailed(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("HRESULT failed"); }

    DX12Framework* m_fw;
    UINT m_size;
    UINT m_cascades;

    ComPtr<ID3D12Resource> m_tex;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_dsvHandles;
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvHandleGPU{};
    UINT m_srvIndex = 0;

    void CreateResource()
    {
        CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS, m_size, m_size, m_cascades, 1, 1, 0,
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
        m_dsvHandles.resize(m_cascades);
        UINT dsvInc = m_fw->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        for (UINT i = 0; i < m_cascades; ++i)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
            dsv.Format = DXGI_FORMAT_D32_FLOAT;
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsv.Flags = D3D12_DSV_FLAG_NONE;
            dsv.Texture2DArray.ArraySize = 1;
            dsv.Texture2DArray.FirstArraySlice = i;
            dsv.Texture2DArray.MipSlice = 0;

            m_dsvHandles[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_fw->GetDSVHandle(), 1 + i, dsvInc);
            m_fw->GetDevice()->CreateDepthStencilView(m_tex.Get(), &dsv, m_dsvHandles[i]);
        }

        m_srvIndex = m_fw->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv.Texture2DArray.MipLevels = 1;
        srv.Texture2DArray.ArraySize = m_cascades;
        srv.Texture2DArray.FirstArraySlice = 0;
        srv.Texture2DArray.MostDetailedMip = 0;

        auto srvCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_fw->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
            m_srvIndex, m_fw->GetSrvDescriptorSize());
        m_fw->GetDevice()->CreateShaderResourceView(m_tex.Get(), &srv, srvCPU);

        m_srvHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            m_fw->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
            m_srvIndex, m_fw->GetSrvDescriptorSize());
    }
};
