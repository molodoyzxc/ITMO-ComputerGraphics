#include "Meshlets.h"
#include <stdexcept>

static inline uint32_t PackTriU8(uint32_t a, uint32_t b, uint32_t c)
{
    return (a & 0xFFu) | ((b & 0xFFu) << 8) | ((c & 0xFFu) << 16);
}

void BuildMeshlets_Greedy(
    const std::vector<uint32_t>& indices,
    uint32_t maxVerts, uint32_t maxPrims,
    std::vector<Meshlet>& outMeshlets,
    std::vector<uint32_t>& outMeshletVertices,
    std::vector<uint32_t>& outMeshletPrimsPacked)
{
    if (indices.size() % 3 != 0) throw std::runtime_error("indices must be triangle list");
    if (maxVerts == 0 || maxVerts > 64) throw std::runtime_error("maxVerts must be 1-64");
    if (maxPrims == 0 || maxPrims > 126) throw std::runtime_error("maxPrims must be 1-126");

    outMeshlets.clear();
    outMeshletVertices.clear();
    outMeshletPrimsPacked.clear();

    std::vector<uint32_t> curVerts; curVerts.reserve(maxVerts);
    std::vector<uint32_t> curPrims; curPrims.reserve(maxPrims);
    std::unordered_map<uint32_t, uint32_t> remap; remap.reserve(maxVerts * 2);

    auto Flush = [&]()
    {
        if (curPrims.empty()) return;

        Meshlet m{};
        m.vertexOffset = (uint32_t)outMeshletVertices.size();
        m.vertexCount  = (uint32_t)curVerts.size();
        m.primOffset   = (uint32_t)outMeshletPrimsPacked.size();
        m.primCount    = (uint32_t)curPrims.size();

        outMeshlets.push_back(m);

        outMeshletVertices.insert(outMeshletVertices.end(), curVerts.begin(), curVerts.end());
        outMeshletPrimsPacked.insert(outMeshletPrimsPacked.end(), curPrims.begin(), curPrims.end());

        curVerts.clear();
        curPrims.clear();
        remap.clear();
    };

    const size_t triCount = indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t)
    {
        uint32_t i0 = indices[t * 3 + 0];
        uint32_t i1 = indices[t * 3 + 1];
        uint32_t i2 = indices[t * 3 + 2];

        uint32_t add = 0;
        add += (remap.find(i0) == remap.end()) ? 1u : 0u;
        add += (remap.find(i1) == remap.end()) ? 1u : 0u;
        add += (remap.find(i2) == remap.end()) ? 1u : 0u;

        if ((curPrims.size() + 1) > maxPrims || (curVerts.size() + add) > maxVerts)
        {
            Flush();
        }

        auto GetLocal = [&](uint32_t gi) -> uint32_t
        {
            auto it = remap.find(gi);
            if (it != remap.end()) return it->second;
            uint32_t li = (uint32_t)curVerts.size();
            curVerts.push_back(gi);
            remap.emplace(gi, li);
            return li;
        };

        uint32_t l0 = GetLocal(i0);
        uint32_t l1 = GetLocal(i1);
        uint32_t l2 = GetLocal(i2);

        curPrims.push_back(PackTriU8(l0, l1, l2));
    }

    Flush();
}

static void CreateStructuredSRV(
    ID3D12Device* device,
    ID3D12Resource* res,
    UINT strideBytes,
    D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.Buffer.FirstElement = 0;
    d.Buffer.NumElements = (UINT)(res->GetDesc().Width / strideBytes);
    d.Buffer.StructureByteStride = strideBytes;
    d.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(res, &d, dst);
}

void CreateMeshletSRVs(
    ID3D12Device* device,
    ID3D12Resource* vertices,
    ID3D12Resource* meshlets,
    ID3D12Resource* meshletVertices,
    ID3D12Resource* meshletPrims,
    D3D12_CPU_DESCRIPTOR_HANDLE heapCpuStart,
    UINT descriptorSize,
    uint32_t srvBase)
{
    auto h0 = CD3DX12_CPU_DESCRIPTOR_HANDLE(heapCpuStart, (INT)srvBase + 0, descriptorSize);
    auto h1 = CD3DX12_CPU_DESCRIPTOR_HANDLE(heapCpuStart, (INT)srvBase + 1, descriptorSize);
    auto h2 = CD3DX12_CPU_DESCRIPTOR_HANDLE(heapCpuStart, (INT)srvBase + 2, descriptorSize);
    auto h3 = CD3DX12_CPU_DESCRIPTOR_HANDLE(heapCpuStart, (INT)srvBase + 3, descriptorSize);

    CreateStructuredSRV(device, vertices, sizeof(MeshVertex), h0);
    CreateStructuredSRV(device, meshlets, sizeof(Meshlet), h1);
    CreateStructuredSRV(device, meshletVertices, sizeof(uint32_t), h2);
    CreateStructuredSRV(device, meshletPrims, sizeof(uint32_t), h3);
}
