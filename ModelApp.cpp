#include "ModelApp.h"
#include "Meshes.h"
#include "d3dx12.h"
#include "DX12Framework.h"
#include <DirectXMath.h>
#include <stdexcept>
#include <math.h>
#include "ModelLoader.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct CB {
    XMFLOAT4X4 World, WVP;
    XMFLOAT4 LightDir, LightColor, Ambient, EyePos, ObjectColor;
    XMFLOAT2 uvScale, uvOffset;
    XMFLOAT4 Ka, Kd, Ks;
    float Ns;
    float pad[3];
};

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

ModelApp::ModelApp(DX12Framework* framework, InputDevice* input)
    : m_framework(framework)
    , m_input(input)
    , m_pipeline(framework)
    , m_cameraX(0), m_cameraY(0), m_cameraZ(-10) // начальная позиция камеры
    , m_lightX(0), m_lightY(-5), m_lightZ(2)   // начальное направление света
    , m_yaw(0), m_pitch(0)                       // углы поворота камеры
{
}

void ModelApp::Initialize()
{
    m_pipeline.Init();

    auto* device = m_framework->GetDevice();
    auto cmdList = m_framework->GetCommandList();
    auto alloc = m_framework->GetCommandAllocator();
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);

    Mesh mesh = ModelLoader::LoadGeometry("Assets\\12281_Container_v2_L2.obj");
    Material material;

    SceneObject Model = {
        mesh,
        {0,0,0,},
        {0,0,0,},
        {0.01f,0.01f,0.01f,},
    };

    SceneObject Cube = {
    CreateCube(),
    {0,5,0,},
    {0,0,0,},
    {3.0f,3.0f,3.0f,},
    };

    Model.LoadMaterial("Assets\\12281_Container_v2_L2.mtl","12281_container");

    m_objects.push_back(Model);
    m_objects.push_back(Cube);

    alloc->Reset();
    cmdList->Reset(alloc, nullptr);

    for (SceneObject& obj : m_objects) {
        obj.CreateBuffers(device,cmdList);
    }

    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* lists[] = { cmdList };
    m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
    m_framework->WaitForGpu();

    // загрузка тесктур
    DirectX::ResourceUploadBatch uploadBatch(device);
    uploadBatch.Begin();

    m_objects[0].LoadTexture(device, uploadBatch, m_framework, L"Assets\\12281_Container_diffuse.jpg");
    m_objects[1].LoadTexture(device, uploadBatch, m_framework, L"Assets\\bricks.dds");

    ThrowIfFailed(alloc->Reset());
    ThrowIfFailed(cmdList->Reset(alloc, nullptr));

    auto finish = uploadBatch.End(m_framework->GetCommandQueue());

    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* Clists[] = { cmdList };
    m_framework->GetCommandQueue()->ExecuteCommandLists(1, Clists);
    finish.wait();

    // CB
    {
        const UINT cbSize = (sizeof(CB) + 255) & ~255;
        const UINT totalSize = cbSize * static_cast<UINT>(m_objects.size());
        const auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_constantBuffer)));
    }
}

void ModelApp::Update(float dt)
{
    if (m_input->IsKeyDown(Keys::L)) m_objects[0].rotation.y += 0.005f;
    if (m_input->IsKeyDown(Keys::J)) m_objects[0].rotation.y -= 0.005f;

    if (m_input->IsKeyDown(Keys::I)) m_objects[0].rotation.x += 0.005f;
    if (m_input->IsKeyDown(Keys::K)) m_objects[0].rotation.x -= 0.005f;

    const float rotationSpeed = 0.02f;

    if (m_input->IsKeyDown(Keys::Left))  m_yaw -= rotationSpeed;
    if (m_input->IsKeyDown(Keys::Right)) m_yaw += rotationSpeed;
    if (m_input->IsKeyDown(Keys::Up))    m_pitch += rotationSpeed;
    if (m_input->IsKeyDown(Keys::Down))  m_pitch -= rotationSpeed;

    const float limit = XM_PIDIV2 - 0.01f;
    if (m_pitch > limit)  m_pitch = limit;
    if (m_pitch < -limit) m_pitch = -limit;

    XMVECTOR forward = XMVectorSet(sinf(m_yaw), 0, cosf(m_yaw), 0);
    XMVECTOR right = XMVectorSet(cosf(m_yaw), 0, -sinf(m_yaw), 0);

    const float moveSpeed = 0.1f;

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
    auto* cmd = m_framework->GetCommandList();
    auto* alloc = m_framework->GetCommandAllocator();
    ThrowIfFailed(cmd->Reset(alloc, nullptr));

    m_framework->BeginFrame();

    float clear[4]{ 0.1f,0.2f,1.0f,1.0f };
    m_framework->ClearColorAndDepthBuffer(clear);

    m_framework->SetViewportAndScissors();

    m_framework->SetRootSignatureAndPSO(m_pipeline.GetRootSignature(), m_pipeline.GetPipelineState());

    CB cb;
    const XMVECTOR eye = XMVectorSet(m_cameraX, m_cameraY, m_cameraZ, 0);
    XMVECTOR forwardDir = XMVectorSet(
        cosf(m_pitch) * sinf(m_yaw),
        sinf(m_pitch),
        cosf(m_pitch) * cosf(m_yaw),
        0.0f
    );

    XMVECTOR at = XMVectorAdd(eye, forwardDir);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    float aspect = m_framework->GetWidth() / m_framework->GetHeight();
    //float fov = 90.0f * XM_PI / 180.0f;
    float fov = XM_PIDIV4;
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspect, 0.1f, 500.f);

    const UINT cbSize = (sizeof(cb) + 255) & ~255;
    BYTE* pMappedData = nullptr;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedData)));

    for (UINT i = 0; i < m_objects.size(); ++i)
    {
        const XMMATRIX world = m_objects[i].GetWorldMatrix();
        XMMATRIX wvp = world * view * proj;
        XMStoreFloat4x4(&cb.World, world);
        XMStoreFloat4x4(&cb.WVP, wvp);
        XMStoreFloat4(&cb.EyePos, eye);
        cb.uvScale = DirectX::XMFLOAT2(1.0f, 1.0f);
        cb.uvOffset = DirectX::XMFLOAT2(0.0f, 0.0f);
        cb.ObjectColor = m_cubeColor;
        cb.LightDir = { m_lightX, m_lightY, m_lightZ, 0 };
        cb.LightColor = { 1,1,1,0 };
        cb.Ambient = { 0.2f,0.2f,0.2f,0 };
        cb.Ka = XMFLOAT4(m_objects[i].material.ambient.x, m_objects[i].material.ambient.y, m_objects[i].material.ambient.z,1.0f);
        cb.Ks = XMFLOAT4(m_objects[i].material.specular.x, m_objects[i].material.specular.y, m_objects[i].material.specular.z,1.0f);
        cb.Kd = XMFLOAT4(m_objects[i].material.diffuse.x, m_objects[i].material.diffuse.y, m_objects[i].material.diffuse.z,1.0f);
        cb.Ns = m_objects[i].material.shininess;

        memcpy(pMappedData + i * cbSize, &cb, sizeof(cb));
    }
    m_constantBuffer->Unmap(0, nullptr);

    cmd->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    {
        ID3D12DescriptorHeap* heaps[] = {
            m_framework->GetSrvHeap(),
            m_framework->GetSamplerHeap()
        };
        cmd->SetDescriptorHeaps(_countof(heaps), heaps);
    }

    for (UINT i = 0; i < m_objects.size(); ++i)
    {
        cmd->SetGraphicsRootConstantBufferView(
            0,
            m_constantBuffer->GetGPUVirtualAddress() + i * cbSize
        );

        CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(
            m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
            m_objects[i].textureID,
            m_framework->GetSrvDescriptorSize()
        );
        cmd->SetGraphicsRootDescriptorTable(1, texHandle);

        CD3DX12_GPU_DESCRIPTOR_HANDLE sampHandle(
            m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart()
        );

        cmd->SetGraphicsRootDescriptorTable(2, sampHandle);

        cmd->IASetVertexBuffers(0, 1, &m_objects[i].vbView);
        cmd->IASetIndexBuffer(&m_objects[i].ibView);

        cmd->DrawIndexedInstanced(
            static_cast<UINT>(m_objects[i].mesh.indices.size()),
            1,
            0,
            0,
            0
        );
    }

    m_framework->EndFrame();
}