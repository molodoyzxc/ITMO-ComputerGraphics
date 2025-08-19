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
    D3D12_INDEX_BUFFER_VIEW ibView;

    XMFLOAT3 position;
    XMFLOAT3 rotation;
    XMFLOAT3 scale;

    std::vector<Mesh> lodMeshes;
    std::vector<ComPtr<ID3D12Resource>> lodVertexBuffers;
    std::vector<ComPtr<ID3D12Resource>> lodVertexUploads;
    std::vector<D3D12_VERTEX_BUFFER_VIEW>             lodVBs;

    std::vector<ComPtr<ID3D12Resource>> lodIndexBuffers;
    std::vector<ComPtr<ID3D12Resource>> lodIndexUploads;
    std::vector<D3D12_INDEX_BUFFER_VIEW> lodIBs;
    std::vector<float> lodDistances = { 0.0f };

    SceneObject() = default;

    SceneObject(
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

    void CreateBuffersForMesh(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const Mesh& mesh,
        ComPtr<ID3D12Resource>& outVB,
        ComPtr<ID3D12Resource>& outVBUpload,
        D3D12_VERTEX_BUFFER_VIEW& outVBView,
        ComPtr<ID3D12Resource>& outIB,
        ComPtr<ID3D12Resource>& outIBUpload,
        D3D12_INDEX_BUFFER_VIEW& outIBView
    );

    void EnsureDefaultLOD() {
        if (lodMeshes.empty()) {
            lodMeshes.resize(1);
            lodMeshes[0] = mesh;
        }
        if (lodDistances.empty()) {
            lodDistances = { 0.0f };
        }
    }
};
