#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <vector>
#include "QuadTree.h"
#include "DX12Framework.h"
#include "Pipeline.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct TerrainMeshLOD 
{
    ComPtr<ID3D12Resource> vb, ib, vbUpload, ibUpload;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW  ibv{};
    UINT indexCount = 0, vertexCount = 0;
};

struct TerrainDrawItem 
{
    TerrainNode* node{};
    int lod = 0;
};

class Terrain 
{
public:
    Terrain(DX12Framework* fw, Pipeline* pipe) : m_fw(fw), m_pipe(pipe) {}

    void Initialize(UINT heightSrvIdx, float worldSize, int maxDepth, float heightScale, float skirtSize, UINT baseGrid);

    void DrawGBuffer(ID3D12GraphicsCommandList* cmd);

    void SetRootAndPSO(ID3D12RootSignature* rs, ID3D12PipelineState* pso) { m_rs = rs; m_pso = pso; }

    void Collect(const XMFLOAT3& cam, const XMFLOAT4 planes[6], const XMFLOAT4X4& viewProj, float screenTau);

    struct MaterialCBCPU
    {
        float useNormalMap = 0;
        UINT diffuseIdx = 0, normalIdx = 0, dispIdx = 0;
        UINT roughIdx = 0, metalIdx = 0, aoIdx = 0, heightDeltaIdx = 0;
        UINT hasDiffuseMap = 0, hasRoughMap = 0, hasMetalMap = 0, hasAOMap = 0;
        XMFLOAT4 baseColor{ 1,1,1, 1.0f };
        float   roughnessValue = 1.0f, metallicValue = 0.0f, aoValue = 1.0f, _pad = 0.0f;
    } material{};

    void CreateCB();

    void SetWorldParams(const XMFLOAT2& originXZ, float worldSize);

    void SetHeightScale(float heightScale);

    int GetDrawTileCount() const { return (int)m_visible.size(); }

    void SetDiffuseTexture(UINT srvIndex) 
    {
        material.diffuseIdx = srvIndex;
        material.hasDiffuseMap = 1;
        memcpy(m_matPtr, &material, sizeof(material));
    }

    void SetNormalMap(UINT srvIndex, bool enabled = true) 
    {
        material.normalIdx = srvIndex;
        material.useNormalMap = enabled ? 1.0f : 0.0f;
        memcpy(m_matPtr, &material, sizeof(material));
    }

    void DisableDiffuse() 
    {
        material.hasDiffuseMap = 0;
        memcpy(m_matPtr, &material, sizeof(material));
    }

    void DisableNormalMap() 
    {
        material.useNormalMap = 0.0f;
        memcpy(m_matPtr, &material, sizeof(material));
    }

    void SetHeightDeltaTexture(UINT srvIndex)
    {
        material.heightDeltaIdx = srvIndex;
        memcpy(m_matPtr, &material, sizeof(material));
    }

private:
    DX12Framework* m_fw{};
    Pipeline* m_pipe{};
    ID3D12RootSignature* m_rs{};
    ID3D12PipelineState* m_pso{};

    std::vector<TerrainMeshLOD> m_lods;
    QuadTree m_tree;
    std::vector<TerrainDrawItem> m_visible;

    ComPtr<ID3D12Resource> m_cb;
    uint8_t* m_cbPtr = nullptr;
    UINT m_cbStride = 0;

    ComPtr<ID3D12Resource> m_matCB;
    uint8_t* m_matPtr = nullptr;
    UINT m_matStride = 0;

    UINT m_dispSrv = 0;

    ComPtr<ID3D12Resource> m_cbObject;
    ComPtr<ID3D12Resource> m_cbMaterial;
    uint8_t* m_pCbObject = nullptr;
    uint8_t* m_pCbMaterial = nullptr;
    XMFLOAT4X4 m_viewProj{};

    XMFLOAT2 m_originXZ{ 0,0 };
    float m_worldSize = 4096.0f;
    int   m_maxDepth = 6;

    void buildLODGrid(UINT N, bool skirts, float skirtSize, TerrainMeshLOD& out);
    static void addTri(std::vector<uint32_t>& idx, uint32_t a, uint32_t b, uint32_t c) { idx.push_back(a); idx.push_back(b); idx.push_back(c); }
    void writeCB(UINT i, const XMFLOAT4X4& world, const XMFLOAT4X4& viewProj, const XMFLOAT4& uvScaleBias);
};
