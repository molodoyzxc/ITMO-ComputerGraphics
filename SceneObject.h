#pragma once
#include "Meshes.h"
#include <DirectXMath.h>
#include <wrl.h>
#include <d3d12.h>
#include "DX12Framework.h"
#include "Material.h"
#include <WICTextureLoader.h>
#include <ResourceUploadBatch.h>

struct SceneObject {
    Mesh mesh;
    XMFLOAT4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    
    UINT texIdx[6];
    // 0 - diff
    // 1 - normal
    // 2 - disp
    // 3 - rough
    // 4 - metallic
    // 5 - AO

    Material material;
    XMFLOAT3 bsCenter;
    float bsRadius;

    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> vertexBufferUpload;
    ComPtr<ID3D12Resource> indexBufferUpload;
    D3D12_VERTEX_BUFFER_VIEW vbView;
    D3D12_INDEX_BUFFER_VIEW  ibView;

    XMFLOAT3 position;   
    XMFLOAT3 rotation;    
    XMFLOAT3 scale;

    SceneObject() = default;

    explicit SceneObject(
        Mesh mesh,
        XMFLOAT3 pos,
        XMFLOAT3 rot,
        XMFLOAT3 scale
    )
    {
        this->mesh = mesh;
        this->position = pos;
        this->rotation = rot;
        this->scale = scale;
    }

    DirectX::XMMATRIX GetWorldMatrix() const
    {
        return XMMatrixScaling(scale.x, scale.y, scale.z) *
            XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z) *
            XMMatrixTranslation(position.x, position.y, position.z);
    }
public:
    void CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
};
