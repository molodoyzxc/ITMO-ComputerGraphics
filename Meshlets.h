#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <wrl.h>
#include <d3d12.h>
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;

struct Meshlet
{
    uint32_t vertexOffset;
    uint32_t vertexCount; 
    uint32_t primOffset;  
    uint32_t primCount;   
};

struct MeshletBuffersGPU
{
    ComPtr<ID3D12Resource> vertices;
    ComPtr<ID3D12Resource> meshlets;
    ComPtr<ID3D12Resource> meshletVertices;
    ComPtr<ID3D12Resource> meshletPrims;

    uint32_t srvBase = 0;
    uint32_t meshletCount = 0;
};

struct MeshVertex
{
    float pos[3];
    float normal[3];
    float uv[2];
    float tangent[3];
    float handed;
};

void BuildMeshlets_Greedy(
    const std::vector<uint32_t>& indices,
    uint32_t maxVerts, uint32_t maxPrims,
    std::vector<Meshlet>& outMeshlets,
    std::vector<uint32_t>& outMeshletVertices,
    std::vector<uint32_t>& outMeshletPrimsPacked);

void CreateMeshletSRVs(
    ID3D12Device* device,
    ID3D12Resource* vertices,
    ID3D12Resource* meshlets,
    ID3D12Resource* meshletVertices,
    ID3D12Resource* meshletPrims,
    D3D12_CPU_DESCRIPTOR_HANDLE heapCpuStart,
    UINT descriptorSize,
    uint32_t srvBase);
