#include "RenderingSystem.h"
#include "AssetLoader.h"
#include "FrustumPlane.h"
#include <filesystem>

struct CB {
    XMFLOAT4X4 World, WVP;
    XMFLOAT4 EyePos, ObjectColor;
    XMFLOAT2 uvScale, uvOffset;
    XMFLOAT4 Ka, Kd, Ks;
    float Ns;
    float pad[3];
};

struct LightCB {
    int       Type;
    int       pad0[3];
    DirectX::XMFLOAT4 LightDir;
    DirectX::XMFLOAT4 LightColor;
    DirectX::XMFLOAT4 AmbientColor;
    DirectX::XMFLOAT4 LightPosRange;
    DirectX::XMFLOAT4 SpotDirInnerCos; 
    DirectX::XMFLOAT4 SpotOuterPad;
    DirectX::XMFLOAT4X4 InvViewProj;
    DirectX::XMFLOAT4 ScreenSize;
};

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

void RenderingSystem::KeyboardControl() {
    const float rotationSpeed = 0.05f;

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

    const float moveSpeed = 3.0f * acceleration;

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
    , m_cameraX(0), m_cameraY(500), m_cameraZ(0) // начальная позиция камеры
    , m_lightX(0), m_lightY(-10), m_lightZ(0)   // начальное направление света
    , m_yaw(0), m_pitch(0)                       // углы поворота камеры
{
}

void RenderingSystem::SetObjects() {
    Mesh mesh = loader.LoadGeometry("Assets\\sphere.obj");
    Material material;

    SceneObject Model = {
        CreateCube(),
        {1.0f,1.0f,1.0f,1.0f},
        {-1,-1,0,},
        {0,0,0,},
        {2.0f,2.0f,2.0f,},
    };
    Model.textureID = 5;

    SceneObject Cube = {
        CreateCube(),
        {1.0f,1.0f,1.0f,1.0f},
        {-1,1,0,},
        {0,0,0,},
        {2.0f,2.0f,2.0f,},
    };

    SceneObject Right = {
        CreateCube(),
        {1.0f,1.0f,1.0f,1.0f,},
        {1,1,0,},
        {0,0,0,},
        {2.0f,2.0f,2.0f,},
    };

    SceneObject Left = {
        CreateCube(),
        {1.0f,1.0f,1.0f,1.0f,},
        {1,-1,0,},
        {0,0,0,},
        {2.0f,2.0f,2.0f,},
    };

    //Model.LoadMaterial("Assets\\12248_Bird_v1_L2.mtl","12248_Bird_v1");

    //m_objects.push_back(Model);
    //m_objects.push_back(Cube);
    //m_objects.push_back(Right);
    //m_objects.push_back(Left);
    m_objects = loader.LoadSceneObjects("Assets\\Sponza\\sponza.obj");
    //for (SceneObject& obj : m_objects) {
    //    obj.scale = { 0.1f,0.1f,0.1f, };
    //}

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

void RenderingSystem::SetLights() 
{
    Light point {};

    lights.push_back(point);
}

void RenderingSystem::LoadTextures() 
{
    ID3D12Device* device = m_framework->GetDevice();
    auto cmdList = m_framework->GetCommandList();
    auto alloc = m_framework->GetCommandAllocator();

    std::filesystem::path sceneObjPath = "Assets\\Sponza\\sponza.obj";
    std::filesystem::path sceneFolder = sceneObjPath.parent_path();

    DirectX::ResourceUploadBatch uploadBatch(device);
    uploadBatch.Begin();

    UINT whiteIdx = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\white.jpg");

    for (auto& obj : m_objects) {
        const std::string& relTex = obj.material.diffuseTexPath;
        if (relTex.empty()) {
            obj.textureID = whiteIdx;
        }
        else {
            std::filesystem::path texName = std::filesystem::path(relTex).filename();
            std::filesystem::path fullPath = sceneFolder / texName;
            obj.textureID = loader.LoadTexture(
                device,
                uploadBatch,
                m_framework,
                fullPath.wstring().c_str()
            );
        }
    }

    auto finish = uploadBatch.End(m_framework->GetCommandQueue());
    finish.wait();
}

void RenderingSystem::Initialize()
{
    m_pipeline.Init();

    m_gbuffer = std::make_unique<GBuffer>(
        m_framework,
        static_cast<UINT>(m_framework->GetWidth()),
        static_cast<UINT>(m_framework->GetHeight()),
        m_framework->GetRtvHeap(), m_framework->GetRtvDescriptorSize(),
        m_framework->GetSrvHeap(), m_framework->GetSrvDescriptorSize()
    );
    m_gbuffer->Initialize();

    auto* device = m_framework->GetDevice();
    auto cmdList = m_framework->GetCommandList();
    auto alloc = m_framework->GetCommandAllocator();
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);

    SetObjects();
    SetLights();

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

    {
        const UINT cbSize = (sizeof(CB) + 255) & ~255;
        const UINT totalSize = cbSize * static_cast<UINT>(m_objects.size());
        const auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_lightBuffer)));
    }
}

void RenderingSystem::Update(float dt)
{
    if (m_input->IsKeyDown(Keys::F1)) lights[0].type = 0;
    if (m_input->IsKeyDown(Keys::F2)) lights[0].type = 1;
    if (m_input->IsKeyDown(Keys::F3)) lights[0].type = 2;

    if (m_input->IsKeyDown(Keys::N)) lights[0].radius -= 1.0f;
    if (m_input->IsKeyDown(Keys::M)) lights[0].radius += 1.0f;

    if (m_input->IsKeyDown(Keys::J)) lights[0].position.x -= 1.0f;
    if (m_input->IsKeyDown(Keys::L)) lights[0].position.x += 1.0f;
    if (m_input->IsKeyDown(Keys::I)) lights[0].position.y += 1.0f;
    if (m_input->IsKeyDown(Keys::K)) lights[0].position.y -= 1.0f;

    KeyboardControl();
}

void RenderingSystem::Render()
{
    auto* cmd = m_framework->GetCommandList();
    auto* alloc = m_framework->GetCommandAllocator();
    ThrowIfFailed(cmd->Reset(alloc, nullptr));
    m_framework->BeginFrame();

    m_gbuffer->Bind(cmd);
    float clearG[4] = { 0.2f, 0.2f, 1.0f, 1.0f };
    m_gbuffer->Clear(cmd, clearG);

    m_framework->SetViewportAndScissors();

    cmd->SetGraphicsRootSignature(m_pipeline.GetRootSignature());
    cmd->SetPipelineState(m_pipeline.GetGBufferPSO());

    {
        ID3D12DescriptorHeap* heaps[] = {
            m_framework->GetSrvHeap(),
            m_framework->GetSamplerHeap()
        };
        cmd->SetDescriptorHeaps(_countof(heaps), heaps);
    }

    CB cb{};
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
    float aspect = static_cast<float>(m_framework->GetWidth()) / m_framework->GetHeight();
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 5000.f);
    XMMATRIX viewProj = view * proj;

    XMFLOAT4 frustumPlanes[6];
    ExtractFrustumPlanes(frustumPlanes, viewProj);

    const UINT cbSize = (sizeof(cb) + 255) & ~255;
    BYTE* pMappedData = nullptr;
    {
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(
            m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedData))
        );
    }

    m_visibleObjects.clear();
    for (UINT i = 0; i < m_objects.size(); ++i)
    {
        const XMMATRIX world = m_objects[i].GetWorldMatrix();
        const XMVECTOR localCenter = XMLoadFloat3(&m_objects[i].bsCenter);
        const float   localRadius = m_objects[i].bsRadius;
        const XMVECTOR worldCenter = XMVector3Transform(localCenter, world);
        const auto& scale = m_objects[i].scale;
        const float   scaleLen = sqrtf(scale.x * scale.x + scale.y * scale.y + scale.z * scale.z);
        const float   worldRadius = localRadius * scaleLen;

        bool visible = true;
        for (int p = 0; p < 6; ++p)
        {
            const XMVECTOR plane = XMLoadFloat4(&frustumPlanes[p]);
            float dist = XMVectorGetX(XMPlaneDotCoord(plane, worldCenter));
            if (dist < -worldRadius + 0.05f * worldRadius) { visible = false; break; }
        }
        if (visible) m_visibleObjects.push_back(&m_objects[i]);
    }

    for (UINT i = 0; i < m_visibleObjects.size(); ++i)
    {
        SceneObject* obj = m_visibleObjects[i];
        XMMATRIX world = obj->GetWorldMatrix();
        XMMATRIX wvp = world * viewProj;

        XMStoreFloat4x4(&cb.World, world);
        XMStoreFloat4x4(&cb.WVP, wvp);
        XMStoreFloat4(&cb.EyePos, eye);

        cb.uvScale = XMFLOAT2(1, 1);
        cb.uvOffset = XMFLOAT2(0, 0);
        cb.ObjectColor = obj->Color;
        cb.Ka = XMFLOAT4(obj->material.ambient.x,
            obj->material.ambient.y,
            obj->material.ambient.z, 1);
        cb.Kd = XMFLOAT4(obj->material.diffuse.x,
            obj->material.diffuse.y,
            obj->material.diffuse.z, 1);
        cb.Ks = XMFLOAT4(obj->material.specular.x,
            obj->material.specular.y,
            obj->material.specular.z, 1);
        cb.Ns = obj->material.shininess;

        memcpy(pMappedData + i * cbSize, &cb, sizeof(cb));
    }
    m_constantBuffer->Unmap(0, nullptr);

    cmd->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    {
        ID3D12DescriptorHeap* heaps2[] = {
            m_framework->GetSrvHeap(),
            m_framework->GetSamplerHeap()
        };
        cmd->SetDescriptorHeaps(_countof(heaps2), heaps2);
    }

    bool switchedToTransparent = false;
    for (UINT i = 0; i < m_visibleObjects.size(); ++i)
    {
        SceneObject* obj = m_visibleObjects[i];
        if (!switchedToTransparent && obj->Color.w != 1.0f) {
            cmd->SetPipelineState(m_pipeline.GetTransparentPSO());
            switchedToTransparent = true;
        }

        cmd->SetGraphicsRootConstantBufferView(
            0,
            m_constantBuffer->GetGPUVirtualAddress() + i * cbSize
        );

        cmd->SetGraphicsRootConstantBufferView(
            1,
            m_lightBuffer->GetGPUVirtualAddress()
        );

        CD3DX12_GPU_DESCRIPTOR_HANDLE texH(
            m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
            obj->textureID,
            m_framework->GetSrvDescriptorSize()
        );
        cmd->SetGraphicsRootDescriptorTable(2, texH);
        CD3DX12_GPU_DESCRIPTOR_HANDLE sampH(
            m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart()
        );
        cmd->SetGraphicsRootDescriptorTable(3, sampH);

        cmd->IASetVertexBuffers(0, 1, &obj->vbView);
        cmd->IASetIndexBuffer(&obj->ibView);
        cmd->DrawIndexedInstanced(
            (UINT)obj->mesh.indices.size(),
            1, 0, 0, 0
        );
    }

    m_gbuffer->TransitionToReadable(cmd);


    LightCB lightCB{};

    XMMATRIX projForDeferred = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 5000.f);
    XMMATRIX viewProjForDeferred = view * projForDeferred;
    XMMATRIX invVP = XMMatrixInverse(nullptr, viewProjForDeferred);
    invVP = XMMatrixTranspose(invVP);
    XMStoreFloat4x4(&lightCB.InvViewProj, invVP);

    for (Light& light : lights) 
    {       
        lightCB.Type = light.type;
        lightCB.LightDir = { light.direction.x, light.direction.y, light.direction.z, 0.0f };
        lightCB.LightColor = { light.color.x, light.color.y, light.color.z, 0.0f };
        lightCB.AmbientColor = { 0.1f, 0.1f, 0.1f, 0.0f };
        lightCB.LightPosRange = { light.position.x, light.position.y, light.position.z, light.radius };
        lightCB.SpotDirInnerCos = { light.spotDirection.x, light.spotDirection.y, light.spotDirection.z, light.innerCone() };
        lightCB.SpotOuterPad = { light.outerCone(), 0.0f, 0.0f, 0.0f };
        lightCB.ScreenSize = { m_framework->GetWidth(), m_framework->GetHeight(), 0.0f, 0.0f };
    }

    {
        BYTE* pLData = nullptr;
        CD3DX12_RANGE lr(0, 0);
        ThrowIfFailed(
            m_lightBuffer->Map(0, &lr, reinterpret_cast<void**>(&pLData))
        );
        memcpy(pLData, &lightCB, sizeof(lightCB));
        m_lightBuffer->Unmap(0, nullptr);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_framework->GetCurrentRTVHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    float clearBB[4] = { 0,0,0,1 };
    cmd->ClearRenderTargetView(rtv, clearBB, 0, nullptr);
    m_framework->SetViewportAndScissors();

    cmd->SetGraphicsRootSignature(m_pipeline.GetDeferredRS());
    cmd->SetPipelineState(m_pipeline.GetDeferredPSO());

    {
        ID3D12DescriptorHeap* defHeaps[] = {
            m_framework->GetSrvHeap(),
            m_framework->GetSamplerHeap()
        };
        cmd->SetDescriptorHeaps(_countof(defHeaps), defHeaps);
    }

    auto srvHandles = m_gbuffer->GetSRVs();
    cmd->SetGraphicsRootDescriptorTable(0, srvHandles[0]);
    cmd->SetGraphicsRootConstantBufferView(1, m_lightBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(2,
        m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart()
    );

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    m_framework->EndFrame();
}
