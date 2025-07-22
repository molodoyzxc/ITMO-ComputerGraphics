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

    // создаёт ресурсы и дескрипторы
    void Initialize();

    // привязать MRT и DSV перед рисованием геометрии
    void Bind(ID3D12GraphicsCommandList* cmdList);

    // очистить целевые буферы
    void Clear(ID3D12GraphicsCommandList* cmdList, const FLOAT clearColor[4]);

    // получить дескрипторы SRV для шейдера освещения
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> GetSRVs() const;

    void TransitionToReadable(ID3D12GraphicsCommandList* cmd);

private:
    void CreateResources();
    void CreateDescriptors();

    DX12Framework* m_framework;
    UINT                        m_width, m_height;

    // буферы G‑Buffer
    ComPtr<ID3D12Resource>      m_rtAlbedo;    // DXGI_FORMAT_R8G8B8A8_UNORM
    ComPtr<ID3D12Resource>      m_rtNormal;    // DXGI_FORMAT_R16G16B16A16_FLOAT
    ComPtr<ID3D12Resource>      m_rtMaterial;  // DXGI_FORMAT_R8G8B8A8_UNORM (например, metalness/roughness)
    ComPtr<ID3D12Resource>      m_depth;       // DXGI_FORMAT_D32_FLOAT

    // дескрипторные кучи и смещения
    ID3D12DescriptorHeap* m_rtvHeap;
    UINT                        m_rtvDescriptorSize;
    UINT                        m_rtvStartIndex; // начало под наши RTV
    ID3D12DescriptorHeap* m_srvHeap;
    UINT                        m_srvDescriptorSize;
    UINT                        m_srvStartIndex; // начало под наши SRV
};
