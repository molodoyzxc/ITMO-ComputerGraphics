#include "RenderingSystem.h"
#include "AssetLoader.h"
#include "FrustumPlane.h"
#include <filesystem>
#include <iostream>
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"


struct ErrorTextures
{
    UINT white;
    UINT roughness;
    UINT metallic;
    UINT normal;
    UINT height;
    UINT abmientOcclusion;
    UINT diffuse;
} errorTextures;

struct CB {
    XMFLOAT4X4 World, ViewProj;
};

struct LightCB 
{
    int       Type;
    int       pad0[3];
    DirectX::XMFLOAT4 LightDir;
    DirectX::XMFLOAT4 LightColor;
    DirectX::XMFLOAT4 LightPosRange;
    DirectX::XMFLOAT4 SpotDirInnerCos; 
    DirectX::XMFLOAT4 SpotOuterPad;
    DirectX::XMFLOAT4X4 InvViewProj;
    DirectX::XMFLOAT4 ScreenSize;
};

struct AmbientCB
{
    XMFLOAT4 AmbientColor;
};

struct TessCB
{
    XMFLOAT3 cameraPos;
    float heightScale;
    float minDist;
    float maxDist;
    float minTess;
    float maxTess;
};

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

void RenderingSystem::KeyboardControl() {
    const float rotationSpeed = 0.03f;

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
    if (m_input->IsKeyDown(Keys::LeftShift))
    {
        acceleration = 3.0f;
    }
    else if (m_input->IsKeyDown(Keys::CapsLock))
    {
        acceleration = 0.1f;
    }
    else
    {
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

void RenderingSystem::CountFPS()
{
    static float fpsAccumulator = 0.0f;
    static int frameCount = 0;

    fpsAccumulator += timer.GetElapsedSeconds();
    frameCount++;

    if (fpsAccumulator >= 1.0f)
    {
        float fps = frameCount / fpsAccumulator;

        wchar_t msg[128];
        swprintf_s(msg, L"[FPS] %.2f\n", fps);
        OutputDebugStringW(msg);

        wchar_t buffer[128];
        swprintf_s(buffer, L"[CAMERA] X: %.2f Y: %.2f Z: %.2f\n", m_cameraX, m_cameraY, m_cameraZ);
        OutputDebugStringW(buffer);

        frameCount = 0;
        fpsAccumulator = 0.0f;
    }
}

RenderingSystem::RenderingSystem(DX12Framework* framework, InputDevice* input)
    : m_framework(framework)
    , m_input(input)
    , m_pipeline(framework)
    , m_cameraX(0), m_cameraY(500), m_cameraZ(0) // начальная позиция камеры
    , m_lightX(0), m_lightY(-1), m_lightZ(0)   // начальное направление света
    , m_yaw(0), m_pitch(0)                       // углы поворота камеры
{
}

void RenderingSystem::SetObjects() {
    Mesh mesh = loader.LoadGeometry("Assets\\Bread\\3DBread004_HQ-4K-JPG.obj");
    Material material;

    SceneObject Model = {
        CreateCube(),
        {0,0,0,},
        {-30,0,0,},
        {1.0f,1.0f,1.0f,},
    };

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

    SceneObject Sphere = {
        CreateSphere(),
        {0,0,0,},
        {0,0,0,},
        {10.0f,10.0f,10.0f,},
    };

    //Model.LoadMaterial("Assets\\12248_Bird_v1_L2.mtl","12248_Bird_v1");

    //m_objects.push_back(Model);
    //m_objects.push_back(Cube);
    //m_objects.push_back(Right);
    //m_objects.push_back(Left);
    m_objects = loader.LoadSceneObjects("Assets\\Sponza\\sponza.obj");

    for (SceneObject& obj : m_objects) {
        obj.scale = { 1.0f, 1.0f, 1.0f };
    }

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

    for (SceneObject& obj : m_objects) {
        obj.CreateBuffers(m_framework->GetDevice(), m_framework->GetCommandList());
    }
}

void RenderingSystem::SetLights() 
{
    Light light {};
    light.type = 0;
    light.spotDirection = {0,0,1};
    light.direction = {0.0f,-1.0f,0.0f};

    Light red{};
    red.color = { 1.0f,0.0f,0.0f };
    red.position = { 800, 500 ,0 };
    red.radius = 200;

    Light green{};
    green.color = { 0.0f,1.0f,0.0f };
    green.position = { -800, 500, 0 };
    green.radius = 100;

    Light blue{};
    blue.color = { 0.0f,0.0f,1.0f };
    blue.position = { 0.0f,300,0 };
    blue.radius = 50;

    lights.push_back(light);
    //lights.push_back(red);
    //lights.push_back(green);
    //lights.push_back(blue);
}

void RenderingSystem::LoadErrorTextures()
{
    ID3D12Device* device = m_framework->GetDevice();
    DirectX::ResourceUploadBatch uploadBatch(device);
    uploadBatch.Begin();

    errorTextures.white = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\white.jpg");
    errorTextures.normal = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\flat_normal.jpg");
    errorTextures.height = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\errorHeight.jpg");
    errorTextures.metallic = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\errorMetallic.jpg");
    errorTextures.roughness = errorTextures.white;
    errorTextures.abmientOcclusion = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\black.jpg");
    errorTextures.diffuse = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\errorDiffuse.jpg");

    auto finish = uploadBatch.End(m_framework->GetCommandQueue());
    finish.wait();
}

void RenderingSystem::LoadTextures()
{
    ID3D12Device* device = m_framework->GetDevice();
    auto cmdList = m_framework->GetCommandList();
    auto alloc = m_framework->GetCommandAllocator();
    std::filesystem::path sceneFolder = L"Assets\\Sponza";

    DirectX::ResourceUploadBatch uploadBatch(device);
    uploadBatch.Begin();

    auto makeFullPath = [&](const std::string& rel,
        std::filesystem::path& outUsed) -> bool
        {
            if (rel.empty()) return false;
            std::filesystem::path p1 = sceneFolder / std::filesystem::path(rel);
            if (std::filesystem::exists(p1)) {
                outUsed = p1;
                return true;
            }
            std::filesystem::path p2 = sceneFolder / std::filesystem::path(rel).filename();
            if (std::filesystem::exists(p2)) {
                outUsed = p2;
                return true;
            }
            return false;
        };

    auto safeLoad = [&](const std::string& rel, UINT fallbackIdx, UINT error) -> UINT
        {
            std::filesystem::path fullPath;
            if (makeFullPath(rel, fullPath))
            {
                try {
                    return loader.LoadTexture(
                        device, uploadBatch, m_framework,
                        fullPath.wstring().c_str()
                    );
                }
                catch (const std::exception& e) {
                    std::wcerr << L"[Error] failed to load " << fullPath.wstring()
                        << L": " << e.what() << std::endl;
                    return error;
                }
            }
            return fallbackIdx;
        };

    for (auto& obj : m_objects)
    {
        obj.diffuseTexID = safeLoad(obj.material.diffuseTexPath, errorTextures.white, errorTextures.diffuse);
        obj.normalTexID = safeLoad(obj.material.normalTexPath, errorTextures.normal, errorTextures.normal);
        obj.dispTexID = safeLoad(obj.material.displacementTexPath, errorTextures.height, errorTextures.height);
        obj.roughnessTexID = safeLoad(obj.material.roughnessTexPath, errorTextures.white, errorTextures.white);
        obj.metallicTexID = safeLoad(obj.material.metallicTexPath, errorTextures.metallic, errorTextures.metallic);
        obj.aoTexID = safeLoad(obj.material.aoTexPath, errorTextures.abmientOcclusion, errorTextures.abmientOcclusion);
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

    alloc->Reset();
    cmdList->Reset(alloc, nullptr);

    SetObjects();
    SetLights();

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
        const UINT lightCBSize = (sizeof(LightCB) + 255) & ~255;
        const UINT maxLights = lights.size();
        const UINT totalSize = lightCBSize * maxLights;

        CD3DX12_RESOURCE_DESC lightDesc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &lightDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_lightBuffer)
        ));
    }

    {
        const UINT ambientCBSize = (sizeof(AmbientCB) + 255) & ~255;

        CD3DX12_RESOURCE_DESC lightDesc = CD3DX12_RESOURCE_DESC::Buffer(ambientCBSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &lightDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_ambientBuffer)
        ));
    }

    {
        const UINT ambientCBSize = (sizeof(AmbientCB) + 255) & ~255;

        CD3DX12_RESOURCE_DESC lightDesc = CD3DX12_RESOURCE_DESC::Buffer(ambientCBSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &lightDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_ambientBuffer)
        ));
    }

    {
        const UINT tessCbSize = (sizeof(TessCB) + 255) & ~255;
        CD3DX12_RESOURCE_DESC tessDesc = CD3DX12_RESOURCE_DESC::Buffer(tessCbSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &tessDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_tessBuffer)
        ));

        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(m_tessBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pTessCbData)));
    }
}

void RenderingSystem::Update(float dt)
{
    //m_objects.back().position = lights[0].position;

    if (m_input->IsKeyDown(Keys::F1)) lights[0].type = 0;
    if (m_input->IsKeyDown(Keys::F2)) lights[0].type = 1;
    if (m_input->IsKeyDown(Keys::F3)) lights[0].type = 2;

    if (m_input->IsKeyDown(Keys::N)) lights[0].radius -= 2.0f;
    if (m_input->IsKeyDown(Keys::M)) lights[0].radius += 2.0f;

    if (m_input->IsKeyDown(Keys::J)) lights[0].position.x -= 2.0f;
    if (m_input->IsKeyDown(Keys::L)) lights[0].position.x += 2.0f;
    if (m_input->IsKeyDown(Keys::O)) lights[0].position.y += 2.0f;
    if (m_input->IsKeyDown(Keys::U)) lights[0].position.y -= 2.0f;
    if (m_input->IsKeyDown(Keys::I)) lights[0].position.z += 2.0f;
    if (m_input->IsKeyDown(Keys::K)) lights[0].position.z -= 2.0f;

    CountFPS();
    KeyboardControl();
}

float heightScale = 2.0f;
float maxTess = 4.0f;
int wire = 0;

void RenderingSystem::Render()
{
    timer.Tick();
    auto* cmd = m_framework->GetCommandList();
    auto* alloc = m_framework->GetCommandAllocator();
    ThrowIfFailed(cmd->Reset(alloc, nullptr));
    m_framework->BeginFrame();

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

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
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 500000.f);
    XMMATRIX viewProj = view * proj;

    TessCB tc;

    const UINT tcSize = (sizeof(tc) + 255) & ~255;
    m_pTessCbData = nullptr;
    {
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(
            m_tessBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pTessCbData))
        );
    }

    {
        ImGui::Begin("Tessallation");

        ImGui::InputFloat("Height scale", &heightScale, 1.0f);
        ImGui::InputFloat("Max tess", &maxTess, 1.0f);
        ImGui::InputInt("Wireframe", &wire);
        ImGui::Text("Wire = %d", wire);

        ImGui::End();
    }

    tc.cameraPos = { m_cameraX, m_cameraY, m_cameraZ };
    tc.heightScale = heightScale;
    tc.minDist = 5000.0f;
    tc.maxDist = 10000.0f;
    tc.maxTess = maxTess;
    tc.minTess = 1.0f;

    memcpy(m_pTessCbData, &tc, sizeof(tc));

    m_tessBuffer->Unmap(0, nullptr);


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
        XMStoreFloat4x4(&cb.ViewProj, viewProj);

        memcpy(pMappedData + i * cbSize, &cb, sizeof(cb));
    }
    m_constantBuffer->Unmap(0, nullptr);

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

        bool useTess = (obj->dispTexID != errorTextures.height);
        if (useTess)
        {
            if (wire)
            {
                cmd->SetPipelineState(m_pipeline.GetGBufferTessellationWireframePSO());
            }
            else
            {   
                cmd->SetPipelineState(m_pipeline.GetGBufferTessellationPSO());
            }
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        }
        else
        {
            cmd->SetPipelineState(m_pipeline.GetGBufferPSO());
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        cmd->SetGraphicsRootConstantBufferView(
            0,
            m_constantBuffer->GetGPUVirtualAddress() + i * cbSize
        );

        cmd->SetGraphicsRootConstantBufferView(
            1,
            m_lightBuffer->GetGPUVirtualAddress()
        );

        cmd->SetGraphicsRootConstantBufferView(
            2,
            m_tessBuffer->GetGPUVirtualAddress()
        );

        CD3DX12_GPU_DESCRIPTOR_HANDLE texH(
            m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
            obj->diffuseTexID,
            m_framework->GetSrvDescriptorSize()
        );
        cmd->SetGraphicsRootDescriptorTable(3, texH);

        CD3DX12_GPU_DESCRIPTOR_HANDLE sampH(
            m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart()
        );
        cmd->SetGraphicsRootDescriptorTable(4, sampH);

        cmd->IASetVertexBuffers(0, 1, &obj->vbView);
        cmd->IASetIndexBuffer(&obj->ibView);
        cmd->DrawIndexedInstanced(
            (UINT)obj->mesh.indices.size(),
            1, 0, 0, 0
        );
    }

    m_gbuffer->TransitionToReadable(cmd);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_framework->GetCurrentRTVHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    float clearBB[4] = { 0,0,0,1 };
    cmd->ClearRenderTargetView(rtv, clearBB, 0, nullptr);
    m_framework->SetViewportAndScissors();

    cmd->SetGraphicsRootSignature(m_pipeline.GetDeferredRS());

    AmbientCB ambientCB = {};
    ambientCB.AmbientColor = { 0.2f, 0.2f, 0.2f, 0.0f };
    UINT8* pAmbientData = nullptr;
    CD3DX12_RANGE writeRange(0, 0);
    m_ambientBuffer->Map(0, &writeRange, reinterpret_cast<void**>(&pAmbientData));
    memcpy(pAmbientData, &ambientCB, sizeof(ambientCB));
    m_ambientBuffer->Unmap(0, nullptr);

    ID3D12DescriptorHeap* defHeaps[] = {
    m_framework->GetSrvHeap(),
    m_framework->GetSamplerHeap()
    };
    cmd->SetDescriptorHeaps(_countof(defHeaps), defHeaps);

    auto srvHandles = m_gbuffer->GetSRVs();
    cmd->SetGraphicsRootDescriptorTable(0, srvHandles[0]);
    cmd->SetGraphicsRootDescriptorTable(3, m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart());

    cmd->SetPipelineState(m_pipeline.GetAmbientPSO());
    cmd->SetGraphicsRootConstantBufferView(2, m_ambientBuffer->GetGPUVirtualAddress());

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(6, 1, 0, 0);

    cmd->SetPipelineState(m_pipeline.GetDeferredPSO());

    const UINT lightCBSize = (sizeof(LightCB) + 255) & ~255;
    BYTE* pLightData = nullptr;
    {
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(m_lightBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pLightData)));
    }

    XMMATRIX projForDeferred = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 5000.f);
    XMMATRIX viewProjForDeferred = view * proj;

    for (UINT i = 0; i < lights.size(); ++i)
    {
        Light& light = lights[i];

        LightCB lightCB{};

        XMMATRIX invVP = XMMatrixInverse(nullptr, viewProjForDeferred);
        XMStoreFloat4x4(&lightCB.InvViewProj, XMMatrixTranspose(invVP));
        lightCB.Type = light.type;
        lightCB.LightDir = { light.direction.x, light.direction.y, light.direction.z, 0 };
        lightCB.LightColor = { light.color.x,     light.color.y,     light.color.z,     0 };
        lightCB.LightPosRange = { light.position.x, light.position.y, light.position.z, light.radius };
        lightCB.SpotDirInnerCos = { light.spotDirection.x, light.spotDirection.y, light.spotDirection.z, light.innerCone() };
        lightCB.SpotOuterPad = { light.outerCone(), 0, 0, 0 };
        lightCB.ScreenSize = { m_framework->GetWidth(), m_framework->GetHeight(), 0, 0 };

        memcpy(pLightData + i * lightCBSize, &lightCB, sizeof(LightCB));
    }
    m_lightBuffer->Unmap(0, nullptr);

    for (UINT i = 0; i < lights.size(); ++i)
    {
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_lightBuffer->GetGPUVirtualAddress() + i * lightCBSize;
        cmd->SetGraphicsRootConstantBufferView(1, cbAddr);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(6, 1, 0, 0);
    }

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);

    m_framework->EndFrame();
}