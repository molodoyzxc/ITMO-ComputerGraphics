#include "CubeApp.h"
#include "Meshes.h"
#include "d3dx12.h"
#include "DX12Framework.h"
#include <DirectXMath.h>
#include <stdexcept>
#include <math.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct CB { XMFLOAT4X4 WVP; XMFLOAT4 LightDir, LightColor, Ambient, EyePos, ObjectColor; XMFLOAT2 uvScale, uvOffset;};

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

CubeApp::CubeApp(DX12Framework* framework, InputDevice* input)
    : m_framework(framework)
    , m_input(input)
    , m_pipeline(framework)
    , m_cameraX(0), m_cameraY(0), m_cameraZ(-10) // начальная позиция камеры
    , m_lightX(0), m_lightY(-5), m_lightZ(-1)   // начальное направление света
    , m_viewX(0), m_viewY(1), m_viewZ(0)
    , m_yaw(0), m_pitch(0)                       // углы поворота камеры
{
}

void CubeApp::Initialize()
{
    m_pipeline.Init();

    auto* device = m_framework->GetDevice();
    auto cmdList = m_framework->GetCommandList();
    auto alloc = m_framework->GetCommandAllocator();
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);

    Mesh mesh = CreateCube ();

    SceneObject Cube = {
    mesh,
    {0,0,0,},
    {0,0,0},
    {1,1,1,},
    };
    SceneObject Platform = {
        mesh,
        {0,-2,0},
        {0,0,0},
        {20,0.5f,20}
    };

    m_objects.push_back(Cube);
    m_objects.push_back(Platform);

    ThrowIfFailed(alloc->Reset());
    ThrowIfFailed(cmdList->Reset(alloc, nullptr));

    for (auto& obj : m_objects)
    {
        obj.materialID = m_framework->GetWhiteTextureSrvIndex();
        obj.CreateBuffers(device, cmdList);
    }

    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* lists[] = { cmdList };
    m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
    m_framework->WaitForGpu();

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

void CubeApp::Update(float dt)
{
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

void CubeApp::Render()
{
    auto* cmd = m_framework->GetCommandList();
    auto* alloc = m_framework->GetCommandAllocator();
    ThrowIfFailed(cmd->Reset(alloc, nullptr));

    m_framework->BeginFrame();

    float clear[4]{ 0.1f,0.2f,1.0f,1.0f };
    m_framework->ClearColorAndDepthBuffer(clear);

    m_framework->SetViewportAndScissors();

    m_framework->SetRootSignatureAndPSO(m_pipeline.GetRootSignature(), m_pipeline.GetPipelineState());

    // матрицы камеры (world * view * proj)
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
        XMStoreFloat4x4(&cb.WVP, wvp);
        XMStoreFloat4(&cb.EyePos, eye);
        cb.uvScale = DirectX::XMFLOAT2(1.0f, 1.0f);
        cb.uvOffset = DirectX::XMFLOAT2(0.0f, 0.0f);
        cb.ObjectColor = m_cubeColor;
        cb.LightDir = { m_lightX, m_lightY, m_lightZ, 0 };
        cb.LightColor = { 1,1,1,0 };
        cb.Ambient = { 0.2f,0.2f,0.2f,0 };

        memcpy(pMappedData + i * cbSize, &cb, sizeof(cb));
    }
    m_constantBuffer->Unmap(0, nullptr);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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
            m_objects[i].materialID,
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