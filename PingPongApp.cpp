#include "Meshes.h"
#include "d3dx12.h"
#include "DX12Framework.h"
#include <DirectXMath.h>
#include <stdexcept>
#include <math.h>
#include "PingPongApp.h"
#include <random>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

PingPongApp::PingPongApp(DX12Framework* framework, InputDevice* input)
    : m_framework(framework)
    , m_input(input)
    , m_pipeline(framework)
    , m_cameraX(0), m_cameraY(0), m_cameraZ(-20) // начальная позиция камеры
    , m_lightX(0), m_lightY(0), m_lightZ(1)   // начальное направление света
    , m_viewX(0), m_viewY(0), m_viewZ(0)
    , m_yaw(0), m_pitch(0)                       // углы поворота камеры
    , scoreLeft(0), scoreRight(0)
    , startSpeedX(0.02f), startSpeedY(0.02f)
{
    m_ballVelocity = { startSpeedX, startSpeedY };
}

std::vector <XMFLOAT4> colors = {
    {1.0f,0.0f,0.0f,1.0f},
    {0.0f,1.0f,0.0f,1.0f},
    {0.0f,0.0f,1.0f,1.0f},
    {1.0f,1.0f,0.0f,1.0f},
    {1.0f,0.0f,1.0f,1.0f},
    {1.0f,1.0f,1.0f,1.0f},
    {1.0f,1.0f,1.0f,1.0f},
};
SceneObject top;
SceneObject bottom;
BoundingBox* topBox;
BoundingBox* bottomBox;
float acceleration = 0.001f;
std::vector<Mesh> RomanDigits = GenerateRomanDigits();

void PingPongApp::Initialize()
{
    m_pipeline.Init(); // Root Signature и PSO

    auto* device = m_framework->GetDevice();
    auto cmdList = m_framework->GetCommandList();
    auto alloc = m_framework->GetCommandAllocator();
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD); // upload-хип

    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_uploadAlloc));
    device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_uploadAlloc.Get(), nullptr,
        IID_PPV_ARGS(&m_uploadList));
    m_uploadList->Close();

    Mesh mesh = CreatePlane();
    float paddleHeight = 4.0f;

    SceneObject UpperBorder = {
    mesh,
    {0,8,0,},
    {0,0,0},
    {50,1,0,},
    };

    SceneObject LowerBorder = {
    mesh,
    {0,-8,0,},
    {0,0,0},
    {50,1,0,},
    };

    SceneObject Left = {
    mesh,
    {-14,0,0,},
    {0,0,0},
    {0.5,paddleHeight,0,},
    };

    SceneObject Right = {
    mesh,
    {14,0,0,},
    {0,0,0},
    {0.5,paddleHeight,0,},
    };

    SceneObject Ball = {
    mesh,
    {0,0,0,},
    {0,0,0},
    {0.25,0.25,0,},
    };

    SceneObject leftScore = {
        RomanDigits[0],
        {-5,6,0,},
        {0,0,0,},
        {1,1,1,},
    };

    SceneObject rightScore = {
        RomanDigits[0],
        {5,6,0,},
        {0,0,0,},
        {1,1,1,},
    };

    m_objects.push_back(Left);
    m_objects.push_back(Right);
    m_objects.push_back(UpperBorder);
    m_objects.push_back(LowerBorder);
    m_objects.push_back(Ball);
    m_objects.push_back(leftScore);
    m_objects.push_back(rightScore);

    paddleHalfHeight = paddleHeight / 2.0f;
    limitTop = m_objects[2].position.y - 0.6f - paddleHalfHeight;
    limitBottom = m_objects[3].position.y + 0.6f + paddleHalfHeight;
    top = m_objects[2];
    bottom = m_objects[3];
    topBox = new BoundingBox(
        top.position,
        {
            top.scale.x / 2,
            top.scale.y / 2,
            top.scale.z / 2,
        });
    bottomBox = new BoundingBox(
        bottom.position,
        {
            bottom.scale.x / 2,
            bottom.scale.y / 2,
            bottom.scale.z / 2,
        });

    ThrowIfFailed(alloc->Reset());
    ThrowIfFailed(cmdList->Reset(alloc, nullptr));

    for (auto& obj : m_objects) {
        obj.CreateBuffers(device, cmdList);
    }

    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* lists[] = { cmdList };
    m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
    m_framework->WaitForGpu();

    // Constant Buffer
    {
        struct CB { XMFLOAT4X4 WVP; XMFLOAT4 LightDir, LightColor, Ambient, EyePos, ObjectColor; };
        const UINT cbSize = (sizeof(CB) + 255) & ~255;
        const UINT totalSize = cbSize * static_cast<UINT>(m_objects.size());
        const auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_constantBuffer)));
    }
}

void PingPongApp::RebuildBuffers() {
    m_framework->WaitForGpu();

    auto* device = m_framework->GetDevice();
    ThrowIfFailed(m_uploadAlloc->Reset());
    ThrowIfFailed(m_uploadList->Reset(m_uploadAlloc.Get(), nullptr));

    for (auto& obj : m_objects) {
        obj.CreateBuffers(device, m_uploadList.Get());
    }
        

    ThrowIfFailed(m_uploadList->Close());
    ID3D12CommandList* lists[] = { m_uploadList.Get() };
    m_framework->GetCommandQueue()->ExecuteCommandLists(_countof(lists), lists);
    m_framework->WaitForGpu();
}

void PingPongApp::ResetBall() {
    m_objects[4].position = { 0,0,0, };

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dir(-0.03f, 0.03f);
    m_ballVelocity = { dir(gen), dir(gen) };
}

void PingPongApp::Update(float dt)
{
    const float rotationSpeed = 0.02f;

    if (m_input->IsKeyDown(Keys::Left))  m_yaw -= rotationSpeed;
    if (m_input->IsKeyDown(Keys::Right)) m_yaw += rotationSpeed;
    //if (m_input->IsKeyDown(Keys::Up))    m_pitch += rotationSpeed;
    //if (m_input->IsKeyDown(Keys::Down))  m_pitch -= rotationSpeed;

    // ограничение угла pitch (наклона)
    const float limit = XM_PIDIV2 - 0.01f;
    if (m_pitch > limit)  m_pitch = limit;
    if (m_pitch < -limit) m_pitch = -limit;

    // Вычисление forward/right векторов камеры
    XMVECTOR forward = XMVectorSet(sinf(m_yaw), 0, cosf(m_yaw), 0);
    XMVECTOR right = XMVectorSet(cosf(m_yaw), 0, -sinf(m_yaw), 0);

    const float moveSpeed = 0.1f;

    //if (m_input->IsKeyDown(Keys::W)) {
    //    XMVECTOR move = XMVectorScale(forward, moveSpeed);
    //    m_cameraX += XMVectorGetX(move);
    //    m_cameraZ += XMVectorGetZ(move);
    //}
    //if (m_input->IsKeyDown(Keys::S)) {
    //    XMVECTOR move = XMVectorScale(forward, -moveSpeed);
    //    m_cameraX += XMVectorGetX(move);
    //    m_cameraZ += XMVectorGetZ(move);
    //}
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

    if (m_input->IsKeyDown(Keys::I)) m_lightZ += 0.1f;
    if (m_input->IsKeyDown(Keys::K)) m_lightZ -= 0.1f;

    // paddle movement
    if (m_input->IsKeyDown(Keys::W)) {
        if (m_objects[0].position.y < limitTop) {
            m_objects[0].position.y += 0.1f;
        }
    }
    if (m_input->IsKeyDown(Keys::S)) {
        if (m_objects[0].position.y > limitBottom) {
            m_objects[0].position.y -= 0.1f;
        }
    }
    if (m_input->IsKeyDown(Keys::Up)) {
        if (m_objects[1].position.y < limitTop) {
            m_objects[1].position.y += 0.1f;
        }
    }
    if (m_input->IsKeyDown(Keys::Down)) {
        if (m_objects[1].position.y > limitBottom) {
            m_objects[1].position.y -= 0.1f;
        }
    }



    // ball movement and collisions 
    auto& ball = m_objects[4];
    auto& leftPaddle = m_objects[0];
    auto& rightPaddle = m_objects[1];

    BoundingBox ballBox(
        ball.position,
        {
        ball.scale.x / 2,
        ball.scale.y / 2,
        ball.scale.z / 2,
        });
    BoundingBox leftBox(
        leftPaddle.position,
        {
            leftPaddle.scale.x / 2,
            leftPaddle.scale.y / 2,
            leftPaddle.scale.z / 2,
        });
    BoundingBox rightBox(
        rightPaddle.position,
        {
            rightPaddle.scale.x / 2,
            rightPaddle.scale.y / 2,
            rightPaddle.scale.z / 2,
        });

    if (ball.position.x < -20.0f) {
        m_objects[6].mesh = RomanDigits[++scoreRight];
        RebuildBuffers();
        ResetBall();
    }
    if (ball.position.x > 20.0f) {
        m_objects[5].mesh = RomanDigits[++scoreLeft];
        RebuildBuffers();
        ResetBall();
    }


    m_ballVelocity.x *= (1.0f + acceleration);
    m_ballVelocity.y *= (1.0f + acceleration);
    ball.position.x += m_ballVelocity.x;
    ball.position.y += m_ballVelocity.y;

    ballBox = BoundingBox(
        ball.position,
        {
        ball.scale.x / 2,
        ball.scale.y / 2,
        ball.scale.z / 2,
        });


    if (ballBox.Intersects(*topBox) || ballBox.Intersects(*bottomBox))
        m_ballVelocity.y *= -1.0f;

    if (ballBox.Intersects(leftBox) || ballBox.Intersects(rightBox))
        m_ballVelocity.x *= -1.0f;
}

void PingPongApp::Render()
{
    auto* cmd = m_framework->GetCommandList();
    auto* alloc = m_framework->GetCommandAllocator();
    ThrowIfFailed(cmd->Reset(alloc, nullptr));

    m_framework->BeginFrame();

    // очистка буфера цвета и глубины
    auto rtv = m_framework->GetCurrentRTVHandle();
    const float clear[4]{ 0.0f,0.0f,0.0f,1.0f };
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_framework->GetDSVHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsvHandle);
    cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
    cmd->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // viewport и scissor-rect
    const D3D12_VIEWPORT vp{ 0,0,m_framework->GetWidth(),m_framework->GetHeight(),0,1 };
    const D3D12_RECT     sr{ 0,0,m_framework->GetWidth(),m_framework->GetHeight() };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sr);

    // Root Signature + PSO
    cmd->SetGraphicsRootSignature(m_pipeline.GetRootSignature());
    cmd->SetPipelineState(m_pipeline.GetPipelineState());

    // матрицы камеры (world * view * proj)
    struct CB { XMFLOAT4X4 WVP; XMFLOAT4 LightDir, LightColor, Ambient, EyePos, ObjectColor; } cb;
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

    // заполнение всего cb
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
        cb.ObjectColor = colors[i];
        cb.LightDir = { m_lightX, m_lightY, m_lightZ, 0 };
        cb.LightColor = { 1,1,1,0 };
        cb.Ambient = { 0.2f,0.2f,0.2f,0 };

        memcpy(pMappedData + i * cbSize, &cb, sizeof(cb));
    }
    m_constantBuffer->Unmap(0, nullptr);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (UINT i = 0; i < m_objects.size(); ++i)
    {
        cmd->SetGraphicsRootConstantBufferView(
            0,
            m_constantBuffer->GetGPUVirtualAddress() + i * cbSize
        );

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