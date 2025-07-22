#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <vector>
#include "DX12Framework.h"

using Microsoft::WRL::ComPtr;

class GBuffer {
public:
    GBuffer(DX12Framework* framework,
        UINT width, UINT height,
        ID3D12DescriptorHeap* rtvHeap, UINT rtvDescriptorSize,
        ID3D12DescriptorHeap* srvHeap, UINT srvDescriptorSize);
    ~GBuffer() = default;

    void Initialize();

    void Bind(ID3D12GraphicsCommandList* cmdList);

    void Clear(ID3D12GraphicsCommandList* cmdList, const FLOAT clearColor[4]);

    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> GetSRVs() const;

    void TransitionToReadable(ID3D12GraphicsCommandList* cmd);

private:
    void CreateResources();
    void CreateDescriptors();

    DX12Framework* m_framework;
    UINT                        m_width, m_height;

    ComPtr<ID3D12Resource>      m_rtAlbedo;    
    ComPtr<ID3D12Resource>      m_rtNormal;    
    ComPtr<ID3D12Resource>      m_rtMaterial;  
    ComPtr<ID3D12Resource>      m_depth;       

    ID3D12DescriptorHeap* m_rtvHeap;
    UINT                        m_rtvDescriptorSize;
    UINT                        m_rtvStartIndex; 
    ID3D12DescriptorHeap* m_srvHeap;
    UINT                        m_srvDescriptorSize;
    UINT                        m_srvStartIndex; 
    UINT                        m_srvNormalIndex;
    UINT                        m_srvMaterialIndex;
};
