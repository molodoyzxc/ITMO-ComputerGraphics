#include "SceneObject.h"
#include <d3d12.h>
#include "d3dx12.h"
#include <stdexcept>
#include <WICTextureLoader.h>

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

void SceneObject::CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {
    const UINT vbSize = UINT(mesh.vertices.size() * sizeof(Vertex));
    const UINT ibSize = UINT(mesh.indices.size() * sizeof(UINT32));

    {
        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC     bufDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault,
            D3D12_HEAP_FLAG_NONE,
            &bufDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&vertexBuffer)
        ));
    }

    {
        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC     bufDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&vertexBufferUpload)
        ));

        void* pData = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(vertexBufferUpload->Map(0, &readRange, &pData));
        memcpy(pData, mesh.vertices.data(), vbSize);
        vertexBufferUpload->Unmap(0, nullptr);

        cmdList->CopyBufferRegion(vertexBuffer.Get(), 0, vertexBufferUpload.Get(), 0, vbSize);

        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            vertexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
        );
        cmdList->ResourceBarrier(1, &barrier);
    }

    {
        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC     bufDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault,
            D3D12_HEAP_FLAG_NONE,
            &bufDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&indexBuffer)
        ));
    }
    {
        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC     bufDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&indexBufferUpload)
        ));

        void* pData = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(indexBufferUpload->Map(0, &readRange, &pData));
        memcpy(pData, mesh.indices.data(), ibSize);
        indexBufferUpload->Unmap(0, nullptr);

        cmdList->CopyBufferRegion(indexBuffer.Get(), 0, indexBufferUpload.Get(), 0, ibSize);
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            indexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_INDEX_BUFFER
        );
        cmdList->ResourceBarrier(1, &barrier);
    }

    vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vbView.StrideInBytes = sizeof(Vertex);
    vbView.SizeInBytes = vbSize;

    ibView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    ibView.Format = DXGI_FORMAT_R32_UINT;
    ibView.SizeInBytes = ibSize;
}

void SceneObject::LoadTexture(ID3D12Device* device, ResourceUploadBatch& uploadBatch, DX12Framework* framework, const wchar_t* filename)
{
    // 1) Записываем команды загрузки текстуры в uploadBatch
    HRESULT hr = DirectX::CreateWICTextureFromFile(
        device,
        uploadBatch,  // <-- теперь передаём uploadBatch, а не cmdList
        filename,
        texture.ReleaseAndGetAddressOf(),
        textureUploadHeap.ReleaseAndGetAddressOf()
    );
    if (FAILED(hr)) {
        throw std::runtime_error("CreateWICTextureFromFile failed");
    }

    // 2) Получаем свободный индекс (слот) для SRV
    UINT srvIndex = framework->AllocateSrvDescriptor();

    // 3) Описываем SRV (предполагаем формат R8G8B8A8_UNORM)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(-1);

    // 4) Вычисляем CPU-дескриптор для SRV-ячейки
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(
        framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
        srvIndex,
        framework->GetSrvDescriptorSize()
    );

    // 5) Создаём сам SRV в указанной ячейке
    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    // 6) Сохраняем номер (слот) SRV в materialID
    materialID = srvIndex;
}
