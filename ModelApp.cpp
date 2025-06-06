#include "ModelApp.h"
#include "Meshes.h"
#include "d3dx12.h"
#include "DX12Framework.h"
#include <DirectXMath.h>
#include <stdexcept>
#include <math.h>
#include "ResourceUploadBatch.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct CB { XMFLOAT4X4 WVP; XMFLOAT4 LightDir, LightColor, Ambient, EyePos, ObjectColor; XMFLOAT2 uvScale, uvOffset; };

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

ModelApp::ModelApp(DX12Framework* framework, InputDevice* input) 
    : m_framework(framework)
    , m_input(input)
    , m_pipeline(framework)
    , m_cameraX(0), m_cameraY(0), m_cameraZ(-10)
    , m_lightX(0), m_lightY(-5), m_lightZ(-1)
    , m_viewX(0), m_viewY(1), m_viewZ(0)
    , m_yaw(0), m_pitch(0)
{
}

void ModelApp::Initialize()
{
    // 1) Инициализируем pipeline (Root Signature + PSO)
    m_pipeline.Init();

    // 2) Получаем device, cmdList, alloc
    ID3D12Device* device = m_framework->GetDevice();
    ID3D12GraphicsCommandList* cmdList = m_framework->GetCommandList();
    ID3D12CommandAllocator* alloc = m_framework->GetCommandAllocator();

    // 3) Сбрасываем allocator/commandList перед загрузкой текстур и VB/IB
    ThrowIfFailed(alloc->Reset());
    ThrowIfFailed(cmdList->Reset(alloc, nullptr));

    // 4) Создаём ResourceUploadBatch и начинаем его
    DirectX::ResourceUploadBatch uploadBatch(device);
    uploadBatch.Begin();

    // 5) Загружаем OBJ-модель (BuildBuffers внутри LoadModelFromFile пишет в cmdList,
    //    LoadTextureAndCreateSRV записывает в uploadBatch)
    std::wstring objPath = L"Assets\\cat.obj";
    bool ok = ModelLoader::LoadModelFromFile(objPath, m_framework, uploadBatch, m_model);
    if (!ok) {
        throw std::runtime_error("Failed to load OBJ model in ModelApp::Initialize");
    }

    // 6) Завершаем uploadBatch ? дописываем загрузку текстур в cmdList
    //    Крайне важно: передавать именно cmdList, а не commandQueue
    auto finish = uploadBatch.End(m_framework->GetCommandQueue());

    // 7) Закрываем cmdList и исполняем его
    ThrowIfFailed(cmdList->Close());
    {
        ID3D12CommandList* lists[] = { cmdList };
        m_framework->GetCommandQueue()->ExecuteCommandLists(_countof(lists), lists);
    }
    finish.wait(); // ждём, пока текстуры и VB/IB загрузятся

    // 8) Создаём constant buffer (один для всей модели)
    {
        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        const UINT cbSize = (sizeof(CB) + 255) & ~255;
        auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_constantBuffer)
        ));
    }
}

void ModelApp::Update(float dt)
{
    const float rotationSpeed = 0.02f;

    if (m_input->IsKeyDown(Keys::Left))  m_yaw -= rotationSpeed;
    if (m_input->IsKeyDown(Keys::Right)) m_yaw += rotationSpeed;
    if (m_input->IsKeyDown(Keys::Up))    m_pitch += rotationSpeed;
    if (m_input->IsKeyDown(Keys::Down))  m_pitch -= rotationSpeed;

    // Ограничение угла pitch (наклона)
    const float limit = XM_PIDIV2 - 0.01f;
    if (m_pitch > limit)  m_pitch = limit;
    if (m_pitch < -limit) m_pitch = -limit;

    // Вычисление forward/right векторов камеры
    XMVECTOR forward = XMVectorSet(sinf(m_yaw), 0, cosf(m_yaw), 0);
    XMVECTOR right = XMVectorSet(cosf(m_yaw), 0, -sinf(m_yaw), 0);

    float acceleration;
    if (m_input->IsKeyDown(Keys::LeftShift)) {
        acceleration = 3.0f;
    }
    else {
        acceleration = 1.0f;
    }

    const float moveSpeed = 0.1f * acceleration;

    if (m_input->IsKeyDown(Keys::W)) {
        XMVECTOR move = XMVectorScale(forward, moveSpeed);
        m_cameraX += XMVectorGetX(move);
        m_cameraZ += XMVectorGetZ(move);
    }
    if (m_input->IsKeyDown(Keys::S)) {
        XMVECTOR move = XMVectorScale(forward, -moveSpeed);
        m_cameraX += XMVectorGetX(move);
        m_cameraZ += XMVectorGetZ(move);
    }
    if (m_input->IsKeyDown(Keys::A)) {
        XMVECTOR move = XMVectorScale(right, -moveSpeed);
        m_cameraX += XMVectorGetX(move);
        m_cameraZ += XMVectorGetZ(move);
    }
    if (m_input->IsKeyDown(Keys::D)) {
        XMVECTOR move = XMVectorScale(right, moveSpeed);
        m_cameraX += XMVectorGetX(move);
        m_cameraZ += XMVectorGetZ(move);
    }

    if (m_input->IsKeyDown(Keys::Q)) m_cameraY -= moveSpeed;
    if (m_input->IsKeyDown(Keys::E)) m_cameraY += moveSpeed;
}

void ModelApp::Render()
{
    // 0) Ждём, пока GPU завершит предыдущий кадр
    m_framework->WaitForGpu();

    // 1) Сбрасываем allocator и cmdList
    ID3D12CommandAllocator* alloc = m_framework->GetCommandAllocator();
    ID3D12GraphicsCommandList* cmd = m_framework->GetCommandList();
    ThrowIfFailed(alloc->Reset());
    ThrowIfFailed(cmd->Reset(alloc, nullptr));

    // 2) Начало кадра
    m_framework->BeginFrame();
    float clearColor[4] = { 0.1f, 0.1f, 1.0f, 1.0f };
    m_framework->ClearColorAndDepthBuffer(clearColor);

    // 3) Устанавливаем viewport и scissors
    m_framework->SetViewportAndScissors();

    // 4) Привязываем корневую сигнатуру и PSO
    m_framework->SetRootSignatureAndPSO(
        m_pipeline.GetRootSignature(),
        m_pipeline.GetPipelineState()
    );

    // 5) Считаем камеры
    XMVECTOR eye = XMVectorSet(m_cameraX, m_cameraY, m_cameraZ, 0.0f);
    XMVECTOR forwardVec = XMVectorSet(
        cosf(m_pitch) * sinf(m_yaw),
        sinf(m_pitch),
        cosf(m_pitch) * cosf(m_yaw),
        0.0f
    );
    XMVECTOR at = XMVectorAdd(eye, forwardVec);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    float aspect = m_framework->GetWidth() / m_framework->GetHeight();
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 1000.0f);

    // 6) Обновляем константный буфер
    {
        CB cb;
        XMMATRIX world = m_model.GetWorldMatrix();
        XMMATRIX wvp = world * view * proj;
        XMStoreFloat4x4(&cb.WVP, wvp);

        cb.LightDir = { m_lightX, m_lightY, m_lightZ, 0.0f };
        cb.LightColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        cb.Ambient = { 0.2f, 0.2f, 0.2f, 1.0f };
        XMStoreFloat4(&cb.EyePos, eye);

        cb.ObjectColor = m_objectColor;
        cb.uvScale = { 1.0f, 1.0f };
        cb.uvOffset = { 0.0f, 0.0f };

        UINT8* pData = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
        memcpy(pData, &cb, sizeof(CB));
        m_constantBuffer->Unmap(0, nullptr);

        cmd->SetGraphicsRootConstantBufferView(
            0,
            m_constantBuffer->GetGPUVirtualAddress()
        );
    }

    // 7) Устанавливаем топологию и дескрипторные кучи (SRV + Sampler)
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12DescriptorHeap* heaps[] = {
        m_framework->GetSrvHeap(),
        m_framework->GetSamplerHeap()
    };
    cmd->SetDescriptorHeaps(_countof(heaps), heaps);

    // 8) Рендер всех MeshPart
    for (const auto& part : m_model.parts) {
        // 8a) SRV для альбедо (или fallback на белую)
        UINT materialIdx = part.materialID;
        UINT srvToBind = m_framework->GetWhiteTextureSrvIndex();
        if (materialIdx < m_model.materials.size()) {
            const Material& mat = m_model.materials[materialIdx];
            if (mat.albedoSrvIndex != UINT_MAX) {
                srvToBind = mat.albedoSrvIndex;
            }
        }
        CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(
            m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
            srvToBind,
            m_framework->GetSrvDescriptorSize()
        );
        cmd->SetGraphicsRootDescriptorTable(1, texHandle);

        // 8b) Самплер (root parameter #2)
        CD3DX12_GPU_DESCRIPTOR_HANDLE sampHandle(
            m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart()
        );
        cmd->SetGraphicsRootDescriptorTable(2, sampHandle);

        // 8c) VB/IB
        cmd->IASetVertexBuffers(0, 1, &part.vbView);
        cmd->IASetIndexBuffer(&part.ibView);

        // 8d) DrawIndexedInstanced
        cmd->DrawIndexedInstanced(
            part.indexCount, // число индексов
            1,                // instanceCount
            0,                // startIndexLocation
            0,                // baseVertexLocation
            0                 // startInstanceLocation
        );
    }

    // 9) Завершаем кадр (барьер и Present) внутри EndFrame()
    m_framework->EndFrame();
}