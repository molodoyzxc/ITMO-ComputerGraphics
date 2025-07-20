#include "RenderingSystem.h"
#include "AssetLoader.h"
#include "FrustumPlane.h"

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

void RenderingSystem::KeyboardControl() {
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

RenderingSystem::RenderingSystem(DX12Framework* framework, InputDevice* input)
    : m_framework(framework)
    , m_input(input)
    , m_pipeline(framework)
    , m_cameraX(0), m_cameraY(0), m_cameraZ(-10) // начальная позиция камеры
    , m_lightX(0), m_lightY(-5), m_lightZ(2)   // начальное направление света
    , m_yaw(0), m_pitch(0)                       // углы поворота камеры
{
}

void RenderingSystem::SetObjects() {
    Mesh mesh = loader.LoadGeometry("Assets\\sphere.obj");
    Material material;

    SceneObject Model = {
        CreateCube(),
        {1.0f,1.0f,1.0f,1.0f},
        {0,0,0,},
        {0,0,0,},
        {2.0f,2.0f,2.0f,},
    };
    Model.textureID = 5;

    SceneObject Cube = {
        CreateCube(),
        {1.0f,1.0f,1.0f,1.0f},
        {0,3,0,},
        {0,0,0,},
        {2.0f,2.0f,2.0f,},
    };

    SceneObject Right = {
        CreateCube(),
        {1.0f,1.0f,1.0f,0.8f,},
        {5,0,0,},
        {0,0,0,},
        {1.0f,10.0f,10.0f,},
    };

    SceneObject Left = {
        CreateCube(),
        {1.0f,1.0f,1.0f,0.8f,},
        {-5,0,0,},
        {0,0,0,},
        {1.0f,10.0f,10.0f,},
    };

    //Model.LoadMaterial("Assets\\12248_Bird_v1_L2.mtl","12248_Bird_v1");

    m_objects.push_back(Model);
    m_objects.push_back(Cube);
    m_objects.push_back(Right);
    m_objects.push_back(Left);

    // culling test
    //for (int i = 0; i < 5; i++) {
    //    SceneObject tmp = {
    //        CreateSphere(20,20,1),
    //        { 0 + i * 5.0f, 0, 0, },
    //        { 0, 0, 0, },
    //        { 1.0f, 1.0f, 1.0f, },
    //    };
    //    m_objects.push_back(tmp);
    //}
}

void RenderingSystem::LoadTextures() 
{
    std::vector<const wchar_t*> textures =
    {
        L"Assets\\bricks.dds",
        L"Assets\\texture.jpg",
        L"Assets\\bonsaiko.png",
        L"Assets\\bigtree.png",
        L"Assets\\Sponza\\lion.tga",
    };

    ID3D12Device* device = m_framework->GetDevice();
    auto cmdList = m_framework->GetCommandList();
    auto alloc = m_framework->GetCommandAllocator();

    DirectX::ResourceUploadBatch uploadBatch(device);
    uploadBatch.Begin();

    loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\white.jpg");

    for (auto& texture : textures) {
        loader.LoadTexture(device, uploadBatch, m_framework, texture);
    }

    auto finish = uploadBatch.End(m_framework->GetCommandQueue());
    finish.wait();
}

void RenderingSystem::Initialize()
{
    m_pipeline.Init();

    auto* device = m_framework->GetDevice();
    auto cmdList = m_framework->GetCommandList();
    auto alloc = m_framework->GetCommandAllocator();
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);

    SetObjects();

    alloc->Reset();
    cmdList->Reset(alloc, nullptr);

    for (SceneObject& obj : m_objects) {
        obj.CreateBuffers(device, cmdList);
    }

    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* lists[] = { cmdList };
    m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
    m_framework->WaitForGpu();

    LoadTextures();
    
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

void RenderingSystem::Update(float dt)
{
    if (m_input->IsKeyDown(Keys::K)) 
    {
        m_objects[0].textureID = 4;
        m_objects[1].textureID = 1;
        m_objects[2].textureID = 2;
        m_objects[3].textureID = 3;
    }

    KeyboardControl();
}

void RenderingSystem::Render()
{
    auto* cmd = m_framework->GetCommandList();
    auto* alloc = m_framework->GetCommandAllocator();
    ThrowIfFailed(cmd->Reset(alloc, nullptr));

    m_framework->BeginFrame();

    float clear[4]{ 0.1f,0.2f,1.0f,1.0f };
    m_framework->ClearColorAndDepthBuffer(clear);

    m_framework->SetViewportAndScissors();

    m_framework->SetRootSignatureAndPSO(m_pipeline.GetRootSignature(), m_pipeline.GetOpaquePSO());

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
    XMMATRIX viewProj = view * proj;
    XMFLOAT4 frustumPlanes[6];
    ExtractFrustumPlanes(frustumPlanes, viewProj);

    const UINT cbSize = (sizeof(cb) + 255) & ~255;
    BYTE* pMappedData = nullptr;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedData)));

    m_visibleObjects.clear();

    // visible objects
    for (UINT i = 0; i < m_objects.size(); ++i)
    {
        const XMMATRIX world = m_objects[i].GetWorldMatrix();

        const XMVECTOR localCenter = XMLoadFloat3(&m_objects[i].bsCenter);
        const float localRadius = m_objects[i].bsRadius;

        const XMVECTOR worldCenter = XMVector3Transform(localCenter, world);

        const auto& scale = m_objects[i].scale;
        const float scaleLength = sqrtf(scale.x * scale.x + scale.y * scale.y + scale.z * scale.z);
        const float worldRadius = localRadius * scaleLength;

        bool visible = true;
        for (int p = 0; p < 6; ++p)
        {
            const XMVECTOR plane = XMLoadFloat4(&frustumPlanes[p]);
            const float dist = XMVectorGetX(XMPlaneDotCoord(plane, worldCenter));
            const float epsilon = 0.05f * worldRadius;
            if (dist < -worldRadius + epsilon) {
                visible = false;
                break;
            }
        }

        if (visible)
            m_visibleObjects.push_back(&m_objects[i]);
    }

    for (UINT i = 0; i < m_visibleObjects.size(); ++i)
    {
        SceneObject* obj = m_visibleObjects[i];
        XMMATRIX world = obj->GetWorldMatrix();
        XMMATRIX wvp = world * viewProj;

        XMStoreFloat4x4(&cb.World, world);
        XMStoreFloat4x4(&cb.WVP, wvp);
        XMStoreFloat4(&cb.EyePos, eye);
        cb.uvScale = XMFLOAT2(1.0f, 1.0f);
        cb.uvOffset = XMFLOAT2(0.0f, 0.0f);
        cb.ObjectColor = obj->Color;
        cb.LightDir = { m_lightX, m_lightY, m_lightZ, 0 };
        cb.LightColor = { 1,1,1,0 };
        cb.Ambient = { 0.2f,0.2f,0.2f,0 };
        cb.Ka = XMFLOAT4(obj->material.ambient.x, obj->material.ambient.y, obj->material.ambient.z, 1.0f);
        cb.Ks = XMFLOAT4(obj->material.specular.x, obj->material.specular.y, obj->material.specular.z, 1.0f);
        cb.Kd = XMFLOAT4(obj->material.diffuse.x, obj->material.diffuse.y, obj->material.diffuse.z, 1.0f);
        cb.Ns = obj->material.shininess;

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

    bool flag = true;
    for (UINT i = 0; i < m_visibleObjects.size(); ++i)
    {

        if (flag && m_visibleObjects[i]->Color.w != 1.0f) {
            m_framework->GetCommandList()->SetPipelineState(m_pipeline.GetTransparentPSO());
            flag = false;
        }
        cmd->SetGraphicsRootConstantBufferView(
            0,
            m_constantBuffer->GetGPUVirtualAddress() + i * cbSize
        );

        CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(
            m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
            m_visibleObjects[i]->textureID,
            m_framework->GetSrvDescriptorSize()
        );
        cmd->SetGraphicsRootDescriptorTable(1, texHandle);

        CD3DX12_GPU_DESCRIPTOR_HANDLE sampHandle(
            m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart()
        );

        cmd->SetGraphicsRootDescriptorTable(2, sampHandle);

        cmd->IASetVertexBuffers(0, 1, &m_visibleObjects[i]->vbView);
        cmd->IASetIndexBuffer(&m_visibleObjects[i]->ibView);

        cmd->DrawIndexedInstanced(
            static_cast<UINT>(m_visibleObjects[i]->mesh.indices.size()),
            1,
            0,
            0,
            0
        );
    }

    m_framework->EndFrame();
}