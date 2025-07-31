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
    
    UINT diffuseTexID = 0;
    UINT normalTexID = 0;
    UINT dispTexID = 0;
    UINT roughnessTexID = 0;
    UINT metallicTexID = 0;
    UINT aoTexID = 0;

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

    SceneObject(const Mesh& m,
        XMFLOAT3 pos,
        XMFLOAT3 rot,
        XMFLOAT3 scl)
        : mesh(m)
        , diffuseTexID(0)
        , position(pos)
        , rotation(rot)
        , scale(scl)
    {
    }

    SceneObject(const Mesh& m,
        XMFLOAT4 color,
        XMFLOAT3 pos,
        XMFLOAT3 rot,
        XMFLOAT3 scl)
        : mesh(m)
        , Color(color)
        , diffuseTexID(0)
        , position(pos)
        , rotation(rot)
        , scale(scl)
    {
    }

    DirectX::XMMATRIX GetWorldMatrix() const {
        using namespace DirectX;
        return XMMatrixScaling(scale.x, scale.y, scale.z) *
            XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z) *
            XMMatrixTranslation(position.x, position.y, position.z);
    }
public:
    void CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
};
