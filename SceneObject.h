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
    std::vector<Mesh> meshes;
    Mesh mesh;
    UINT materialID = 0;
    Material material;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBufferUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBufferUpload;
    D3D12_VERTEX_BUFFER_VIEW vbView;
    D3D12_INDEX_BUFFER_VIEW  ibView;

    DirectX::XMFLOAT3 position;   
    DirectX::XMFLOAT3 rotation;    
    DirectX::XMFLOAT3 scale;       

    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> textureUploadHeap;

    SceneObject() = default;

    SceneObject(const Mesh& m,
        UINT matID,
        DirectX::XMFLOAT3 pos,
        DirectX::XMFLOAT3 rot,
        DirectX::XMFLOAT3 scl)
        : mesh(m)
        , materialID(matID)
        , position(pos)
        , rotation(rot)
        , scale(scl)
    {
    }

    SceneObject(const Mesh& m,
        DirectX::XMFLOAT3 pos,
        DirectX::XMFLOAT3 rot,
        DirectX::XMFLOAT3 scl)
        : mesh(m)
        , materialID(0)
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
    void LoadTexture(
        ID3D12Device* device,
        ResourceUploadBatch& uploadBatch,
        DX12Framework* framework,
        const wchar_t* filename);
    void LoadMaterial(const std::string& mtlFile, const std::string& materialName);
};
