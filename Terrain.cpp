#include "Terrain.h"
#include "d3dx12.h"
using namespace DirectX;

struct VSObjCB 
{
    XMFLOAT4X4 World;
    XMFLOAT4X4 ViewProj;
    XMFLOAT4   UVScaleBias;
};

void Terrain::Initialize(UINT heightSrvIdx, float worldSize, int maxDepth, float heightScale, float skirtSize, UINT baseGrid)
{
    m_dispSrv = heightSrvIdx;
    material.dispIdx = heightSrvIdx;
    material.baseColor.w = heightScale;

    m_worldSize = worldSize;
    m_maxDepth = maxDepth;
    m_originXZ = { 0,0 };

    m_lods.clear();
    for (UINT N = baseGrid; N >= 8; N >>= 1)
    {
        TerrainMeshLOD lod{};
        buildLODGrid(N, true, skirtSize, lod);
        m_lods.push_back(std::move(lod));
        if (N == 8) break;
    }

    m_tree.SetHeightMax(heightScale);
    m_tree.Build({ m_originXZ.x, 0, m_originXZ.y }, m_worldSize, m_maxDepth);

    CreateCB();
}

void Terrain::CreateCB()
{
    auto* dev = m_fw->GetDevice();
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);

    m_cbStride = ((UINT)sizeof(VSObjCB) + 255) & ~255u;
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(m_cbStride * 65536);
    dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cb));
    m_cb->Map(0, nullptr, (void**)&m_cbPtr);

    m_matStride = ((UINT)sizeof(MaterialCBCPU) + 255) & ~255u;
    auto desc2 = CD3DX12_RESOURCE_DESC::Buffer(m_matStride);
    dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc2,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_matCB));
    m_matCB->Map(0, nullptr, (void**)&m_matPtr);
    memcpy(m_matPtr, &material, sizeof(material));
}

void Terrain::buildLODGrid(UINT N, bool skirts, float skirtSize, TerrainMeshLOD& out)
{
    struct V { XMFLOAT3 pos, nrm; XMFLOAT2 uv; XMFLOAT3 tan; float hand; };
    std::vector<V> v; v.reserve((N + 1) * (N + 1) + (skirts ? 4 * (N + 1) : 0));
    auto put = [&](float x, float z, float u, float vuv) 
        {
            V vv{}; vv.pos = { x,0,z }; vv.nrm = { 0,1,0 }; vv.uv = { u,vuv }; vv.tan = { 1,0,0 }; vv.hand = 1.0f; v.push_back(vv);
        };

    for (UINT z = 0; z <= N; ++z)
        for (UINT x = 0; x <= N; ++x) 
        {
            float fx = (float)x / N, fz = (float)z / N;
            put(fx, fz, fx, fz);
        }

    std::vector<uint32_t> idx;
    auto id = [&](UINT x, UINT z) { return z * (N + 1) + x; };
    for (UINT z = 0; z < N; ++z)
        for (UINT x = 0; x < N; ++x)
        {
            uint32_t a = id(x, z), b = id(x + 1, z), c = id(x, z + 1), d = id(x + 1, z + 1);
            addTri(idx, a, b, d); addTri(idx, a, d, c);
        }

    if (skirts) 
    {
        auto pushSkirtStrip = [&](UINT count, auto baseIndexGetter) 
            {
                UINT baseStart = (UINT)v.size();
                for (UINT i = 0; i < count; ++i)
                {
                    V top = v[baseIndexGetter(i)];
                    V bot = top; bot.pos.y -= skirtSize;
                    v.push_back(bot);
                }

                for (UINT i = 0; i < count - 1; ++i) 
                {
                    uint32_t t0 = baseIndexGetter(i), t1 = baseIndexGetter(i + 1);
                    uint32_t b0 = baseStart + i, b1 = baseStart + i + 1;
                    addTri(idx, t0, t1, b1);
                    addTri(idx, t0, b1, b0);
                }
            };

        pushSkirtStrip(N + 1, [&](UINT i) { return id(i, 0);   });
        pushSkirtStrip(N + 1, [&](UINT i) { return id(i, N);   });
        pushSkirtStrip(N + 1, [&](UINT i) { return id(0, i);   });
        pushSkirtStrip(N + 1, [&](UINT i) { return id(N, i);   });
    }

    auto* dev = m_fw->GetDevice();
    auto* cmd = m_fw->GetCommandList();
    out.vertexCount = (UINT)v.size();
    out.indexCount = (UINT)idx.size();

    m_fw->CreateDefaultBuffer(cmd, v.data(), sizeof(V) * v.size(), out.vb, out.vbUpload);
    m_fw->CreateDefaultBuffer(cmd, idx.data(), sizeof(uint32_t) * idx.size(), out.ib, out.ibUpload);

    out.vbv.BufferLocation = out.vb->GetGPUVirtualAddress();
    out.vbv.SizeInBytes = (UINT)(sizeof(V) * v.size());
    out.vbv.StrideInBytes = sizeof(V);

    out.ibv.BufferLocation = out.ib->GetGPUVirtualAddress();
    out.ibv.SizeInBytes = (UINT)(sizeof(uint32_t) * idx.size());
    out.ibv.Format = DXGI_FORMAT_R32_UINT;
}

void Terrain::Collect(const XMFLOAT3& cam, const XMFLOAT4 planes[6], const XMFLOAT4X4& viewProj, float screenTau)
{
    m_viewProj = viewProj;

    std::vector<TerrainNode*> nodes;
    nodes.reserve(256);
    m_tree.CollectLOD(cam, screenTau, viewProj, planes, nodes);

    m_visible.clear();
    m_visible.reserve(nodes.size());
    const int maxLodIndex = m_lods.empty() ? 0 : int(m_lods.size() - 1); 
    for (TerrainNode* n : nodes)
    {
        TerrainDrawItem item{};
        item.node = n;
        const int clamped = min(n->level, maxLodIndex);
        item.lod = maxLodIndex - clamped;
        m_visible.push_back(item);
    }
}

void Terrain::writeCB(UINT i, const XMFLOAT4X4& world, const XMFLOAT4X4& viewProj, const XMFLOAT4& uvScaleBias)
{
    auto* dst = m_cbPtr + i * m_cbStride;
    VSObjCB cb{ world, viewProj, uvScaleBias };
    memcpy(dst, &cb, sizeof(cb));
}

void Terrain::DrawGBuffer(ID3D12GraphicsCommandList* cmd)
{
    cmd->SetGraphicsRootSignature(m_rs);
    cmd->SetPipelineState(m_pso);

    cmd->SetGraphicsRootConstantBufferView(5, m_matCB->GetGPUVirtualAddress());

    UINT drawIndex = 0;
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (const auto& it : m_visible)
    {
        const TerrainNode* n = it.node;

        using namespace DirectX;
        XMMATRIX S = XMMatrixScaling(n->size, 1.0f, n->size);
        XMMATRIX T = XMMatrixTranslation(n->origin.x, 0.0f, n->origin.z);
        XMFLOAT4X4 W; XMStoreFloat4x4(&W, S * T);

        XMFLOAT4 uvSB{ n->uv1.x, n->uv1.y, n->uv0.x, n->uv0.y };

        writeCB(drawIndex, W, m_viewProj, uvSB);

        D3D12_GPU_VIRTUAL_ADDRESS objCB = m_cb->GetGPUVirtualAddress() + UINT64(drawIndex) * m_cbStride;
        cmd->SetGraphicsRootConstantBufferView(0, objCB);

        const auto& lod = m_lods[it.lod];
        cmd->IASetVertexBuffers(0, 1, &lod.vbv);
        cmd->IASetIndexBuffer(&lod.ibv);
        cmd->DrawIndexedInstanced(lod.indexCount, 1, 0, 0, 0);

        ++drawIndex;
    }
}

void Terrain::SetWorldParams(const DirectX::XMFLOAT2& originXZ, float worldSize)
{
    m_originXZ = originXZ;
    m_worldSize = worldSize;

    m_tree.Build({ m_originXZ.x, 0, m_originXZ.y }, m_worldSize, m_maxDepth);
}

void Terrain::SetHeightScale(float heightScale)
{
    material.baseColor.w = heightScale;
    m_tree.SetHeightMax(heightScale);
    memcpy(m_matPtr, &material, sizeof(material));
}