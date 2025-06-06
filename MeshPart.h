#pragma once

#include <vector>
#include <d3d12.h>
#include <wrl.h>
#include "Vertexes.h"
#include "Material.h"

using Microsoft::WRL::ComPtr;

struct MeshPart {
    // Исходные CPU-данные (скопированы из MeshLoader)
    std::vector<Vertex>   vertices;
    std::vector<UINT32>   indices;
    UINT                  materialID = UINT_MAX;
    // индекс в векторе Model::materials

  // Количество индексов (когда рисуем, DrawIndexedInstanced берёт именно indexCount)
    UINT indexCount = 0;

    // GPU-ресурсы:
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> vertexUploadBuffer;
    ComPtr<ID3D12Resource> indexUploadBuffer;

    D3D12_VERTEX_BUFFER_VIEW vbView;
    D3D12_INDEX_BUFFER_VIEW  ibView;

    // Строим GPU-буферы на основе данных vertices/indices
    void BuildBuffers(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList
    );
};
