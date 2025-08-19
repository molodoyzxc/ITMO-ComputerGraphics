#include "RenderingSystem.h"
#include "AssetLoader.h"
#include "FrustumPlane.h"
#include <filesystem>
#include <iostream>
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "ShadowMap.h"

using namespace DirectX;

struct CB { XMFLOAT4X4 World, ViewProj; };

struct LightCB {
    int Type; int pad0[3];
    XMFLOAT4 LightDir;
    XMFLOAT4 LightColor;
    XMFLOAT4 LightPosRange;
    XMFLOAT4 SpotDirInnerCos;
    XMFLOAT4 SpotOuterPad;
    XMFLOAT4X4 InvViewProj;
    XMFLOAT4 ScreenSize;
    XMFLOAT4X4 LightViewProj;
    XMFLOAT4   ShadowParams;
};

struct AmbientCB { XMFLOAT4 AmbientColor; };

struct TessCB {
    XMFLOAT3 cameraPos; float heightScale;
    float minDist; float maxDist;
    float minTess; float maxTess;
};

struct MaterialCB {
    float useNormalMap; // 0 – geometry, 1 – normal map
    UINT diffuseIdx;
    UINT normalIdx;
    UINT dispIdx;
    UINT roughIdx;
    UINT metalIdx;
    UINT aoIdx;
    float pad[1];
};

struct ErrorTextures {
    UINT white{};
    UINT roughness{};
    UINT metallic{};
    UINT normal{};
    UINT height{};
    UINT ambientOcclusion{};
    UINT diffuse{};
} errorTextures;

RenderingSystem::RenderingSystem(DX12Framework* framework, InputDevice* input)
    : m_framework(framework)
    , m_input(input)
    , m_pipeline(framework)
{
}

void RenderingSystem::KeyboardControl()
{
    if (m_input->IsKeyDown(Keys::Left))  m_yaw -= rotationSpeed;
    if (m_input->IsKeyDown(Keys::Right)) m_yaw += rotationSpeed;
    if (m_input->IsKeyDown(Keys::Up))    m_pitch += rotationSpeed;
    if (m_input->IsKeyDown(Keys::Down))  m_pitch -= rotationSpeed;

    const float limit = XM_PIDIV2 - 0.01f;
    m_pitch = std::clamp(m_pitch, -limit, limit);

    const XMVECTOR forward = XMVectorSet(sinf(m_yaw), 0, cosf(m_yaw), 0);
    const XMVECTOR right = XMVectorSet(cosf(m_yaw), 0, -sinf(m_yaw), 0);

    float moveSpeed = cameraSpeed;
    if (m_input->IsKeyDown(Keys::LeftShift)) moveSpeed *= acceleration;
    else if (m_input->IsKeyDown(Keys::CapsLock)) moveSpeed *= deceleration;

    auto moveXZ = [&](float s, XMVECTOR dir) {
        XMVECTOR mv = XMVectorScale(dir, s);
        cameraPos.x += XMVectorGetX(mv);
        cameraPos.z += XMVectorGetZ(mv);
        };

    if (m_input->IsKeyDown(Keys::W)) moveXZ(moveSpeed, forward);
    if (m_input->IsKeyDown(Keys::S)) moveXZ(-moveSpeed, forward);
    if (m_input->IsKeyDown(Keys::A)) moveXZ(-moveSpeed, right);
    if (m_input->IsKeyDown(Keys::D)) moveXZ(moveSpeed, right);

    if (m_input->IsKeyDown(Keys::Q)) cameraPos.y -= moveSpeed;
    if (m_input->IsKeyDown(Keys::E)) cameraPos.y += moveSpeed;
}

void RenderingSystem::CountFPS()
{
    static float acc = 0.f; static int frames = 0;
    acc += timer.GetElapsedSeconds(); frames++;
    if (acc >= 1.0f) { m_currentFPS = frames / acc; acc = 0.f; frames = 0; }
}

void RenderingSystem::SetObjects()
{
    m_objects = loader.LoadSceneObjectsLODs
    (
        {
            //"Assets\\SponzaCrytek\\sponza.obj", 
            "Assets\\Test2\\test.obj", 
        },
        { 0.0f, }
    );

    m_objectScale = 11.0f;
    for (auto& obj : m_objects) obj.scale = { m_objectScale, m_objectScale, m_objectScale };

    for (auto& obj : m_objects) {
        const size_t L = obj.lodMeshes.size();
        obj.lodVertexBuffers.resize(L);
        obj.lodVertexUploads.resize(L);
        obj.lodVBs.resize(L);
        obj.lodIndexBuffers.resize(L);
        obj.lodIndexUploads.resize(L);
        obj.lodIBs.resize(L);

        for (size_t i = 0; i < L; ++i) {
            obj.CreateBuffersForMesh(
                m_framework->GetDevice(),
                m_framework->GetCommandList(),
                obj.lodMeshes[i],
                obj.lodVertexBuffers[i],
                obj.lodVertexUploads[i],
                obj.lodVBs[i],
                obj.lodIndexBuffers[i],
                obj.lodIndexUploads[i],
                obj.lodIBs[i]
            );
        }
    }
}

void RenderingSystem::SetLights()
{
    Light l{};
    l.type = 0;
    l.color = { 1,1,1 };
    l.spotDirection = { 0,0,1 };
    l.direction = direction;
    lights.push_back(l);
}

void RenderingSystem::LoadErrorTextures()
{
    auto* device = m_framework->GetDevice();
    DirectX::ResourceUploadBatch uploadBatch(device);
    uploadBatch.Begin();

    errorTextures.white = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\white.jpg");
    errorTextures.normal = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\flat_normal.png");
    errorTextures.height = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\errorHeight.jpg");
    errorTextures.metallic = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\errorMetallic.jpg");
    errorTextures.roughness = errorTextures.white;
    errorTextures.ambientOcclusion = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\black.jpg");
    errorTextures.diffuse = loader.LoadTexture(device, uploadBatch, m_framework, L"Assets\\Error\\errorDiffuse.jpg");

    auto finish = uploadBatch.End(m_framework->GetCommandQueue());
    finish.wait();
}

void RenderingSystem::LoadTextures()
{
    auto* device = m_framework->GetDevice();
    DirectX::ResourceUploadBatch uploadBatch(device);
    uploadBatch.Begin();

    //std::filesystem::path sceneFolder = L"Assets\\SponzaCrytek";
    std::filesystem::path sceneFolder = L"Assets\\Test2";

    auto makeFullPath = [&](const std::string& rel, std::filesystem::path& out)->bool 
        {
        if (rel.empty()) return false;
        auto p1 = sceneFolder / rel;
        if (std::filesystem::exists(p1)) { out = p1; return true; }
        auto p2 = sceneFolder / std::filesystem::path(rel).filename();
        if (std::filesystem::exists(p2)) { out = p2; return true; }
        return false;
        };

    auto safeLoad = [&](const std::string& rel, UINT fallback, UINT onError)->UINT 
        {
        std::filesystem::path full;
        if (makeFullPath(rel, full)) 
        {
            try { return loader.LoadTexture(device, uploadBatch, m_framework, full.wstring().c_str()); }
            catch (...) { return onError; }
        }
        return fallback;
        };

    for (auto& obj : m_objects)
    {
        obj.texIdx[0] = safeLoad(obj.material.diffuseTexPath, errorTextures.white, errorTextures.diffuse);
        obj.texIdx[1] = safeLoad(obj.material.normalTexPath, errorTextures.normal, errorTextures.normal);
        obj.texIdx[2] = safeLoad(obj.material.displacementTexPath, errorTextures.height, errorTextures.height);
        obj.texIdx[3] = safeLoad(obj.material.roughnessTexPath, errorTextures.white, errorTextures.roughness);
        obj.texIdx[4] = safeLoad(obj.material.metallicTexPath, errorTextures.metallic, errorTextures.metallic);
        obj.texIdx[5] = safeLoad(obj.material.aoTexPath, errorTextures.ambientOcclusion, errorTextures.ambientOcclusion);
    }

    auto finish = uploadBatch.End(m_framework->GetCommandQueue());
    finish.wait();
}

void RenderingSystem::CreateConstantBuffers()
{
    auto* device = m_framework->GetDevice();
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);

    {
        const UINT cbSize = Align256(sizeof(CB));
        const UINT totalSize = cbSize * static_cast<UINT>(m_objects.size());
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)));
        CD3DX12_RANGE rr(0, 0); m_constantBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pCbData));
    }

    {
        const UINT cbSize = Align256(sizeof(LightCB));
        const UINT totalSize = cbSize * static_cast<UINT>(lights.size());
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_lightBuffer)));
        CD3DX12_RANGE rr(0, 0); m_lightBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pLightData));
    }

    {
        const UINT totalSize = Align256(sizeof(AmbientCB));
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_ambientBuffer)));
        CD3DX12_RANGE rr(0, 0); m_ambientBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pAmbientData));
    }

    {
        const UINT cbSize = Align256(sizeof(MaterialCB));
        const UINT totalSize = cbSize * static_cast<UINT>(m_objects.size());
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_materialBuffer)));
        CD3DX12_RANGE rr(0, 0); m_materialBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pMaterialData));
    }

    {
        const UINT totalSize = Align256(sizeof(TessCB));
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_tessBuffer)));
        CD3DX12_RANGE rr(0, 0); m_tessBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pTessCbData));
    }

    {
        const UINT cbSize = Align256(sizeof(CB));
        const UINT totalSize = cbSize * static_cast<UINT>(m_objects.size());
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_shadowBuffer)));
        CD3DX12_RANGE rr(0, 0);
        m_shadowBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pShadowCbData));
    }

}

void RenderingSystem::Initialize()
{
    cmd = m_framework->GetCommandList();

    m_pipeline.Init();

    m_gbuffer = std::make_unique<GBuffer>(
        m_framework,
        static_cast<UINT>(m_framework->GetWidth()),
        static_cast<UINT>(m_framework->GetHeight()),
        m_framework->GetRtvHeap(), m_framework->GetRtvDescriptorSize(),
        m_framework->GetSrvHeap(), m_framework->GetSrvDescriptorSize()
    );
    m_gbuffer->Initialize();

    m_shadow = std::make_unique<ShadowMap>(m_framework, 2048 * 8);
    m_shadow->Initialize();

    auto* alloc = m_framework->GetCommandAllocator();

    alloc->Reset();
    cmd->Reset(alloc, nullptr);

    SetObjects();
    SetLights();

    ThrowIfFailed(cmd->Close());
    ID3D12CommandList* lists[] = { cmd };
    m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
    m_framework->WaitForGpu();

    LoadErrorTextures();
    LoadTextures();
    CreateConstantBuffers();
}

void RenderingSystem::Update(float)
{
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
    UpdateUI();

    BuildViewProj();
    ExtractVisibleObjects();
    UpdatePerObjectCBs();
    UpdateTessellationCB();

    ShadowPass();

    m_gbuffer->Bind(cmd);
    const float clearG[4] = { 0.2f, 0.2f, 1.0f, 1.0f };
    m_gbuffer->Clear(cmd, clearG);
    m_framework->SetViewportAndScissors();

    GeometryPass();

    m_gbuffer->TransitionToReadable(cmd);

    DeferredPass();

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);

    m_framework->EndFrame();
}

void RenderingSystem::UpdateUI()
{
    {
        ImGui::Begin("Camera");

        ImGui::InputFloat("Camera speed", &cameraSpeed, 1.0f);
        ImGui::InputFloat("Acceleration", &acceleration, 1.0f);
        ImGui::InputFloat("Deceleration", &deceleration, 0.05f);
        ImGui::InputFloat("Rotation speed", &rotationSpeed, 0.01f);
        ImGui::SliderFloat("Fake cam Z", &m_fakeCameraZ, 0.0f, 2000.0f);
        
        ImGui::Text("Camera pos: %f %f %f", cameraPos.x, cameraPos.y, cameraPos.z);

        ImGui::End();
    }
    

    {
        ImGui::Begin("Object");
        
        ImGui::InputInt("Object idx", &objectIdx, 1);

        ImGui::SliderFloat("Rot x deg", &m_objectRotationDeg.x, 0.0f, 360.0f);
        ImGui::SliderFloat("Rot y deg", &m_objectRotationDeg.y, 0.0f, 360.0f);
        ImGui::SliderFloat("Rot z deg", &m_objectRotationDeg.z, 0.0f, 360.0f);

        if (!m_objects.empty()) {
            m_objects[objectIdx].rotation = XMFLOAT3(
                XMConvertToRadians(m_objectRotationDeg.x),
                XMConvertToRadians(m_objectRotationDeg.y),
                XMConvertToRadians(m_objectRotationDeg.z)
            );

            ImGui::InputFloat("Scale", &m_objectScale, 5.0f);
            m_objects[objectIdx].scale = { m_objectScale, m_objectScale, m_objectScale };

            ImGui::InputFloat("Pos x", &m_objects[objectIdx].position.x, 1.0f);
            ImGui::InputFloat("Pos y", &m_objects[objectIdx].position.y, 1.0f);
            ImGui::InputFloat("Pos z", &m_objects[objectIdx].position.z, 1.0f);
        }

        ImGui::InputFloat("Use normal map (0/1)", &m_useNormalMap, 1.0f);

        ImGui::End();
    }

    {
        ImGui::Begin("Tessellation");

        ImGui::InputFloat("Height scale", &m_heightScale, 1.0f);
        ImGui::InputFloat("Max tess", &m_maxTess, 1.0f);
        ImGui::Checkbox("Wireframe", &m_wireframe);
        ImGui::Text("FPS: %.2f", m_currentFPS);

        ImGui::End();
    }

    {
        ImGui::Begin("Light");

        ImGui::SliderFloat("Light x", &direction.x, -50.0f, 50.0f);
        ImGui::SliderFloat("Light y", &direction.y, -50.0f, 50.0f);
        ImGui::SliderFloat("Light z", &direction.z, -50.0f, 50.0f);
        lights[0].direction = direction;

        ImGui::End();
    }
}

void RenderingSystem::BuildViewProj()
{
    const XMVECTOR eye = XMLoadFloat3(&cameraPos);
    const XMVECTOR forwardDir = XMVectorSet(
        cosf(m_pitch) * sinf(m_yaw),
        sinf(m_pitch),
        cosf(m_pitch) * cosf(m_yaw),
        0.0f
    );
    const XMVECTOR at = XMVectorAdd(eye, forwardDir);
    const XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    view = XMMatrixLookAtLH(eye, at, up);

    const float aspect = static_cast<float>(m_framework->GetWidth()) / m_framework->GetHeight();
    proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, m_near, m_far);
    viewProj = view * proj;
}

void RenderingSystem::UpdateTessellationCB()
{
    TessCB t{};
    t.cameraPos = cameraPos;
    t.heightScale = m_heightScale;
    t.minDist = 5000.0f;
    t.maxDist = 10000.0f;
    t.minTess = 1.0f;
    t.maxTess = m_maxTess;

    memcpy(m_pTessCbData, &t, sizeof(t));
}

void RenderingSystem::ExtractVisibleObjects()
{
    XMFLOAT4 planes[6];
    ExtractFrustumPlanes(planes, viewProj);

    m_visibleObjects.clear();
    m_visibleObjects.reserve(m_objects.size());

    for (auto& o : m_objects) {
        const XMMATRIX world = o.GetWorldMatrix();
        const XMVECTOR localCenter = XMLoadFloat3(&o.bsCenter);
        const float    localRadius = o.bsRadius;

        const XMVECTOR worldCenter = XMVector3Transform(localCenter, world);
        const auto& s = o.scale;
        const float    scaleLen = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
        const float    worldRadius = localRadius * scaleLen;

        bool visible = true;
        for (int p = 0; p < 6; ++p) {
            const XMVECTOR plane = XMLoadFloat4(&planes[p]);
            const float dist = XMVectorGetX(XMPlaneDotCoord(plane, worldCenter));
            if (dist < -worldRadius + 0.05f * worldRadius) { visible = false; break; }
        }
        if (visible) m_visibleObjects.push_back(&o);
    }
}

void RenderingSystem::UpdatePerObjectCBs()
{
    const UINT cbSize = Align256(sizeof(CB));
    const UINT materialSize = Align256(sizeof(MaterialCB));

    CB cb{};
    MaterialCB mcb{};

    for (size_t i = 0; i < m_visibleObjects.size(); ++i) {
        const SceneObject* obj = m_visibleObjects[i];

        const XMMATRIX world = obj->GetWorldMatrix();
        XMStoreFloat4x4(&cb.World, world);
        XMStoreFloat4x4(&cb.ViewProj, viewProj);

        mcb.useNormalMap = m_useNormalMap;
        mcb.diffuseIdx = obj->texIdx[0];
        mcb.normalIdx = obj->texIdx[1];
        mcb.dispIdx = obj->texIdx[2];
        mcb.roughIdx = obj->texIdx[3];
        mcb.metalIdx = obj->texIdx[4];
        mcb.aoIdx = obj->texIdx[5];

        memcpy(m_pCbData + static_cast<UINT>(i) * cbSize, &cb, sizeof(cb));
        memcpy(m_pMaterialData + static_cast<UINT>(i) * materialSize, &mcb, sizeof(mcb));
    }
}

void RenderingSystem::UpdateLightCB()
{
    const UINT lightCBSize = Align256(sizeof(LightCB));
    XMMATRIX invVP = XMMatrixInverse(nullptr, viewProj);

    for (size_t i = 0; i < lights.size(); ++i) {
        Light& L = lights[i];
        LightCB cb{};

        XMStoreFloat4x4(&cb.InvViewProj, invVP);
        cb.Type = L.type;
        cb.LightDir = { L.direction.x, L.direction.y, L.direction.z, 0 };
        cb.LightColor = { L.color.x, L.color.y, L.color.z, 0 };
        cb.LightPosRange = { L.position.x, L.position.y, L.position.z, L.radius };
        cb.SpotDirInnerCos = { L.spotDirection.x, L.spotDirection.y, L.spotDirection.z, L.innerCone() };
        cb.SpotOuterPad = { L.outerCone(), 0, 0, 0 };
        cb.ScreenSize = { static_cast<float>(m_framework->GetWidth()), static_cast<float>(m_framework->GetHeight()), 0, 0 };

        cb.LightViewProj = m_lightViewProj;
        cb.ShadowParams = { 1.0f / m_shadow->Size(), 0.001f, 0, 0 };

        memcpy(m_pLightData + static_cast<UINT>(i) * lightCBSize, &cb, sizeof(cb));
    }

    AmbientCB amb{};
    amb.AmbientColor = { 0.2f, 0.2f, 0.2f, 0.0f };
    memcpy(m_pAmbientData, &amb, sizeof(amb));
}

void RenderingSystem::SetCommonHeaps()
{
    ID3D12DescriptorHeap* heaps[] = {
        m_framework->GetSrvHeap(),
        m_framework->GetSamplerHeap()
    };
    cmd->SetDescriptorHeaps(_countof(heaps), heaps);
}

void RenderingSystem::GeometryPass()
{
    SetCommonHeaps();

    cmd->SetGraphicsRootSignature(m_pipeline.GetRootSignature());

    bool switchedToTransparent = false;

    const UINT cbSize = Align256(sizeof(CB));
    const UINT materialSize = Align256(sizeof(MaterialCB));

    const XMFLOAT3 fakeCamPos = { 0, 0, m_fakeCameraZ };

    for (size_t i = 0; i < m_visibleObjects.size(); ++i) {
        SceneObject* obj = m_visibleObjects[i];

        const float dist = XMVectorGetX(XMVector3Length(
            XMLoadFloat3(&fakeCamPos) - XMLoadFloat3(&obj->position)));
        int lod = static_cast<int>(obj->lodDistances.size()) - 1;
        for (int j = 0; j + 1 < static_cast<int>(obj->lodDistances.size()); ++j) {
            if (dist < obj->lodDistances[j + 1]) { lod = j; break; }
        }

        if (!switchedToTransparent && obj->Color.w != 1.0f) {
            cmd->SetPipelineState(m_pipeline.GetTransparentPSO());
            switchedToTransparent = true;
        }

        const bool useTess = (obj->texIdx[2] != errorTextures.height);
        if (useTess) {
            cmd->SetPipelineState(m_wireframe ? m_pipeline.GetGBufferTessellationWireframePSO()
                : m_pipeline.GetGBufferTessellationPSO());
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        }
        else {
            cmd->SetPipelineState(m_pipeline.GetGBufferPSO());
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        cmd->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + static_cast<UINT>(i) * cbSize);
        cmd->SetGraphicsRootConstantBufferView(1, m_lightBuffer->GetGPUVirtualAddress());
        cmd->SetGraphicsRootConstantBufferView(2, m_tessBuffer->GetGPUVirtualAddress());

        auto srvStart = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        cmd->SetGraphicsRootDescriptorTable(3, srvStart);

        auto sampStart = m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart();
        cmd->SetGraphicsRootDescriptorTable(4, sampStart);

        cmd->SetGraphicsRootConstantBufferView(5, m_materialBuffer->GetGPUVirtualAddress() + static_cast<UINT>(i) * materialSize);

        cmd->IASetVertexBuffers(0, 1, &obj->lodVBs[lod]);
        cmd->IASetIndexBuffer(&obj->lodIBs[lod]);
        cmd->DrawIndexedInstanced((UINT)obj->lodMeshes[lod].indices.size(), 1, 0, 0, 0);
    }
}

void RenderingSystem::DeferredPass()
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_framework->GetCurrentRTVHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const float clearBB[4] = { 0,0,0,1 };
    cmd->ClearRenderTargetView(rtv, clearBB, 0, nullptr);
    m_framework->SetViewportAndScissors();

    cmd->SetGraphicsRootSignature(m_pipeline.GetDeferredRS());
    SetCommonHeaps();

    auto srvs = m_gbuffer->GetSRVs();
    cmd->SetGraphicsRootDescriptorTable(0, srvs[0]);
    cmd->SetGraphicsRootDescriptorTable(4, m_shadow->Srv());
    cmd->SetGraphicsRootDescriptorTable(3, m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart());

    cmd->SetPipelineState(m_pipeline.GetAmbientPSO());
    cmd->SetGraphicsRootConstantBufferView(2, m_ambientBuffer->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(6, 1, 0, 0);

    cmd->SetPipelineState(m_pipeline.GetDeferredPSO());

    UpdateLightCB();

    const UINT lightCBSize = Align256(sizeof(LightCB));
    for (size_t i = 0; i < lights.size(); ++i) {
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_lightBuffer->GetGPUVirtualAddress() + static_cast<UINT>(i) * lightCBSize;
        cmd->SetGraphicsRootConstantBufferView(1, cbAddr);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(6, 1, 0, 0);
    }
}

void RenderingSystem::BuildLightViewProj()
{
    XMVECTOR L = XMVector3Normalize(XMLoadFloat3(&direction));
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    const float dotY = XMVectorGetX(XMVector3Dot(L, up));
    if (fabsf(dotY) > 0.99f) up = XMVectorSet(0, 0, 1, 0);

    XMMATRIX invVP = XMMatrixInverse(nullptr, viewProj);
    XMVECTOR clip[8] = {
        XMVectorSet(-1,-1,0,1), XMVectorSet(1,-1,0,1),
        XMVectorSet(1, 1,0,1), XMVectorSet(-1, 1,0,1),
        XMVectorSet(-1,-1,1,1), XMVectorSet(1,-1,1,1),
        XMVectorSet(1, 1,1,1), XMVectorSet(-1, 1,1,1),
    };
    XMVECTOR frustumWS[8];
    XMVECTOR center = XMVectorZero();
    for (int i = 0; i < 8; ++i) {
        XMVECTOR v = XMVector4Transform(clip[i], invVP);
        v = XMVectorScale(v, 1.0f / XMVectorGetW(v));
        frustumWS[i] = v;
        center = XMVectorAdd(center, v);
    }
    center = XMVectorScale(center, 1.0f / 8.0f);

    const float dist = m_far;
    XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(L, dist));
    XMMATRIX lv = XMMatrixLookAtLH(eye, center, up);

    XMVECTOR minv = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
    XMVECTOR maxv = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);
    for (int i = 0; i < 8; ++i) {
        XMVECTOR p = XMVector3TransformCoord(frustumWS[i], lv);
        minv = XMVectorMin(minv, p);
        maxv = XMVectorMax(maxv, p);
    }
    float minX = XMVectorGetX(minv), maxX = XMVectorGetX(maxv);
    float minY = XMVectorGetY(minv), maxY = XMVectorGetY(maxv);
    float minZ = XMVectorGetZ(minv), maxZ = XMVectorGetZ(maxv);

    const float shadowSize = m_shadow->Size();
    float w = (maxX - minX);
    float h = (maxY - minY);
    float worldUnitsPerTexel = max(w, h) / shadowSize;

    float cx = 0.5f * (minX + maxX);
    float cy = 0.5f * (minY + maxY);
    cx = floorf(cx / worldUnitsPerTexel) * worldUnitsPerTexel;
    cy = floorf(cy / worldUnitsPerTexel) * worldUnitsPerTexel;

    float hx = 0.5f * w;
    float hy = 0.5f * h;
    minX = cx - hx;  maxX = cx + hx;
    minY = cy - hy;  maxY = cy + hy;

    const float pad = 50.0f;
    minX -= pad; maxX += pad; minY -= pad; maxY += pad;
    minZ = max(0.1f, minZ - pad);
    maxZ = maxZ + pad;

    XMMATRIX lp = XMMatrixOrthographicOffCenterLH(minX, maxX, minY, maxY, minZ, maxZ);

    XMMATRIX lvp = lv * lp;
    XMStoreFloat4x4(&m_lightViewProj, lvp);
}

void RenderingSystem::ShadowPass()
{
    BuildLightViewProj();

    const UINT cbSize = Align256(sizeof(CB));
    for (size_t i = 0; i < m_visibleObjects.size(); ++i) {
        SceneObject* obj = m_visibleObjects[i];
        CB cb{};
        XMStoreFloat4x4(&cb.World, obj->GetWorldMatrix());
        cb.ViewProj = m_lightViewProj;
        memcpy(m_pShadowCbData + (UINT)i * cbSize, &cb, sizeof(cb));
    }

    auto* cl = m_framework->GetCommandList();

    auto toWrite = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadow->Resource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );
    cl->ResourceBarrier(1, &toWrite);

    auto dsv = m_shadow->Dsv();
    cl->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    cl->ClearDepthStencilView(m_shadow->Dsv(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    auto vp = m_shadow->GetViewport();
    auto sc = m_shadow->GetScissor();
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);

    cl->SetGraphicsRootSignature(m_pipeline.GetRootSignature());
    SetCommonHeaps();

    cl->SetPipelineState(m_pipeline.GetShadowPSO());

    const UINT materialSize = Align256(sizeof(MaterialCB));
    for (size_t i = 0; i < m_visibleObjects.size(); ++i) {
        SceneObject* obj = m_visibleObjects[i];

        cl->SetGraphicsRootConstantBufferView(0, m_shadowBuffer->GetGPUVirtualAddress() + (UINT)i * cbSize);

        int lod = (int)obj->lodMeshes.size() - 1;
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl->IASetVertexBuffers(0, 1, &obj->lodVBs[lod]);
        cl->IASetIndexBuffer(&obj->lodIBs[lod]);
        cl->DrawIndexedInstanced((UINT)obj->lodMeshes[lod].indices.size(), 1, 0, 0, 0);
    }

    auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadow->Resource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cl->ResourceBarrier(1, &toRead);
}
