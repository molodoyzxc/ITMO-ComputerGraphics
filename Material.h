#pragma once
#include <string>
#include <DirectXMath.h>

class DX12Framework;
namespace DirectX { class ResourceUploadBatch; }

struct Material
{
    DirectX::XMFLOAT3 ambient = { 0.1f, 0.1f, 0.1f };
    DirectX::XMFLOAT3 diffuse = { 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 specular = { 0.5f, 0.5f, 0.5f };
    float             shininess = 32.0f;

    std::wstring diffusePath;
    std::wstring specularPath;
    std::wstring normalPath;

    UINT diffuseSrvIndex = UINT_MAX;
    UINT specularSrvIndex = UINT_MAX;
    UINT normalSrvIndex = UINT_MAX;
    bool hasNormalMap = false;

    Material() = default;

    void LoadTextures(
        ID3D12Device* device,
        DirectX::ResourceUploadBatch& uploadBatch,
        DX12Framework* framework);
};
