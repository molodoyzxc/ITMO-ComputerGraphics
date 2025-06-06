#include "MeshPart.h"
#include <stdexcept>
#include "d3dx12.h"

static inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("HRESULT failed in MeshPart::BuildBuffers");
}

void MeshPart::BuildBuffers(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList
) {
    // 1. Создаём и заполняем vertexBuffer + upload
    {
        UINT vbSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));

        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&vertexBuffer)
        ));

        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&vertexUploadBuffer)
        ));

        // Карта памяти для vertexUploadBuffer
        UINT8* pData = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(vertexUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
        memcpy(pData, vertices.data(), vbSize);
        vertexUploadBuffer->Unmap(0, nullptr);

        // Запись копирования в cmdList
        cmdList->CopyBufferRegion(
            vertexBuffer.Get(), 0,
            vertexUploadBuffer.Get(), 0,
            vbSize
        );
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            vertexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
        );
        cmdList->ResourceBarrier(1, &barrier);
    }

    // 2. Создаём indexBuffer + upload аналогично
    {
        UINT ibSize = static_cast<UINT>(indices.size() * sizeof(UINT32));

        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&indexBuffer)
        ));

        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&indexUploadBuffer)
        ));

        UINT8* pData = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(indexUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
        memcpy(pData, indices.data(), ibSize);
        indexUploadBuffer->Unmap(0, nullptr);

        cmdList->CopyBufferRegion(
            indexBuffer.Get(), 0,
            indexUploadBuffer.Get(), 0,
            ibSize
        );
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            indexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_INDEX_BUFFER
        );
        cmdList->ResourceBarrier(1, &barrier);
    }

    // 3. Заполняем vbView и ibView
    vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vbView.StrideInBytes = sizeof(Vertex);
    vbView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(Vertex));

    ibView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    ibView.Format = DXGI_FORMAT_R32_UINT;
    ibView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(UINT32));
}