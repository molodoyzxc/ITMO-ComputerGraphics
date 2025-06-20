#include "SolarSystemApp.h"
#include "Meshes.h"
#include "d3dx12.h"
#include "DX12Framework.h"
#include <DirectXMath.h>
#include <stdexcept>
#include <math.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct CB { XMFLOAT4X4 WVP; XMFLOAT4 LightDir, LightColor, Ambient, EyePos, ObjectColor; XMFLOAT2 uvScale, uvOffset; };

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

SolarSystemApp::SolarSystemApp(DX12Framework* framework, InputDevice* input)
    : m_framework(framework)
    , m_input(input)
    , m_pipeline(framework)
    , m_cameraX(0), m_cameraY(0), m_cameraZ(-10) // начальная позиция камеры
    , m_lightX(0), m_lightY(-5), m_lightZ(-1)   // начальное направление света
    , m_viewX(0), m_viewY(1), m_viewZ(0)
    , m_yaw(0), m_pitch(0)                       // углы поворота камеры
{
}

std::vector<float> orbitalPeriods = { 88, 225, 365, 687, 4333, 10759, 30685, 60190, 90560 };

void SolarSystemApp::Initialize()
{
    m_pipeline.Init();

    auto* device = m_framework->GetDevice();
    auto cmdList = m_framework->GetCommandList();
    auto alloc = m_framework->GetCommandAllocator();
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);

    Mesh mesh = CreateCube();

    SceneObject Sun = {
    mesh,
    {0,0,0,},
    {0,0,0},
    {5,5,5,},
    };

    SceneObject Mercury = {
    mesh,
    {10,0,0,},
    {0,0,0},
    {0.38f,0.38f,0.38f,},
    };

    SceneObject Venus = {
    mesh,
    {15,0,0,},
    {0,0,0},
    {0.95f,0.95f,0.95f,},
    };

    SceneObject Earth = {
    mesh,
    {20,0,0,},
    {0,0,0},
    {1.4,1.4,1.4,},
    };

    SceneObject Mars = {
    mesh,
    {25,0,0,},
    {0,0,0},
    {0.53f,0.53f,0.53f,},
    };

    SceneObject Jupiter = {
    mesh,
    {30,0,0,},
    {0,0,0},
    {10.97f,10.97f,10.97f,},
    };

    SceneObject Saturn = {
    mesh,
    {35,0,0,},
    {0,0,0},
    {9.14f,9.14f,9.14f,},
    };

    SceneObject Uranus = {
    mesh,
    {40,0,0,},
    {0,0,0},
    {3.98f,3.98f,3.98f,},
    };

    SceneObject Neptune = {
    mesh,
    {45,0,0,},
    {0,0,0},
    {3.87f,3.87f,3.87f,},
    };

    SceneObject Pluto = {
    mesh,
    {50,0,0,},
    {0,0,0},
    {0.18f,0.18f,0.18f,},
    };

    m_objects.push_back(Sun);
    m_objects.push_back(Mercury);
    m_objects.push_back(Venus);
    m_objects.push_back(Earth);
    m_objects.push_back(Mars);
    m_objects.push_back(Jupiter);
    m_objects.push_back(Saturn);
    m_objects.push_back(Uranus);
    m_objects.push_back(Neptune);
    m_objects.push_back(Pluto);

    ThrowIfFailed(alloc->Reset());
    ThrowIfFailed(cmdList->Reset(alloc, nullptr));

    for (auto& obj : m_objects)
    {
        obj.textureID = m_framework->GetWhiteTextureSrvIndex();
        obj.CreateBuffers(device, cmdList);
    }

    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* lists[] = { cmdList };
    m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
    m_framework->WaitForGpu();

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

void SolarSystemApp::Update(float dt)
{
    m_objects[0].rotation.y += 0.001f;
    float speedScale = 1000.0f;
    int radius = 20;
    float rotation = 0.01f;
    for (int i = 1; i < m_objects.size(); i++) {
        float speed = (1.0f / orbitalPeriods[i - 1]) * speedScale;
        m_objects[i].position = { cosf(speed * dt) * radius * i, 0, sinf(speed * dt) * radius * i, };
        m_objects[i].rotation.y += rotation;
        speed += 0.1f;
        rotation += 0.005f;
    }

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

void SolarSystemApp::Render()
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
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 500.f);

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
        cb.ObjectColor = { 1.0f, 1.0f, 1.0f, 1.0f, };
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