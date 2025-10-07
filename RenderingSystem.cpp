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

struct LightCB 
{
    int Type; int pad0[3];
    XMFLOAT4 LightDir;
    XMFLOAT4 LightColor;
    XMFLOAT4 LightPosRange;
    XMFLOAT4 SpotDirInnerCos;
    XMFLOAT4 SpotOuterPad;
    XMFLOAT4X4 InvViewProj;
    XMFLOAT4 ScreenSize;
    XMFLOAT4X4 LightViewProj[4];
    XMFLOAT4 CascadeSplits;
    XMFLOAT4X4 View;
    XMFLOAT4 ShadowParams;
    XMFLOAT4 CameraPos;
    XMFLOAT4 ShadowMaskParams;
};

struct AmbientCB { XMFLOAT4 AmbientColor; };

struct TessCB {
    XMFLOAT3 cameraPos; float heightScale;
    float minDist; float maxDist;
    float minTess; float maxTess;
};

struct MaterialCB
{
    float useNormalMap;
    UINT diffuseIdx;
    UINT normalIdx;
    UINT dispIdx;

    UINT roughIdx;
    UINT metalIdx;
    UINT aoIdx;
    UINT hasDiffuseMap;

    XMFLOAT4 baseColor;

    float roughnessValue;
    float metallicValue;
    float aoValue;
    UINT hasRoughMap;

    UINT hasMetalMap;
    UINT hasAOMap;
    float _padM;
};

struct PostCB
{
    float Exposure;
    float Gamma;
    float VignetteStrength;
    float VignettePower;

    XMFLOAT2 VignetteCenter;
    XMFLOAT2 InvResolution;

    int   Tonemap;
    int   _padPost;       
    float Saturation;     
    float PosterizeLevels;

    float PixelateSize;
    float _pad2;       
    float _pad3;       
    float _pad4;       
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
            "Assets\\TestPBR\\TestPBR.obj", 
            //"Assets\\Can\\Gas_can.obj", 
            //"Assets\\LOD\\bunnyLOD0.obj", 
            //"Assets\\LOD\\bunnyLOD1.obj", 
            //"Assets\\LOD\\bunnyLOD2.obj", 
            //"Assets\\LOD\\bunnyLOD3.obj", 
            //"Assets\\TestShadows\\wall.obj", 
            //"Assets\\TestShadows\\floor.obj", 
        },
        { 0.0f, 500.0f, 1000.0f, 1500.0f, }
    );

    m_objectScale = 1.1f;
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

    try 
    {
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
    catch (...) 
    {
        uploadBatch.End(m_framework->GetCommandQueue()).wait();
        throw;
    }
}

void RenderingSystem::LoadTextures()
{
    auto* device = m_framework->GetDevice();
    DirectX::ResourceUploadBatch uploadBatch(device);
    uploadBatch.Begin();

    //std::filesystem::path sceneFolder = L"Assets\\SponzaCrytek";
    std::filesystem::path sceneFolder = L"Assets\\TestPBR";
    //std::filesystem::path sceneFolder = L"Assets\\Can";
    //std::filesystem::path sceneFolder = L"Assets\\LOD";
    //std::filesystem::path sceneFolder = L"Assets\\TestShadows";

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
        CD3DX12_RANGE rr(0, 0);
        m_constantBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pCbData));
    }

    {
        const UINT cbSize = Align256(sizeof(LightCB));
        const UINT totalSize = cbSize * static_cast<UINT>(lights.size());
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_lightBuffer)));
        CD3DX12_RANGE rr(0, 0);
        m_lightBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pLightData));
    }

    {
        const UINT totalSize = Align256(sizeof(AmbientCB));
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_ambientBuffer)));
        CD3DX12_RANGE rr(0, 0);
        m_ambientBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pAmbientData));
    }

    {
        const UINT cbSize = Align256(sizeof(MaterialCB));
        const UINT totalSize = cbSize * static_cast<UINT>(m_objects.size());
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_materialBuffer)));
        CD3DX12_RANGE rr(0, 0);
        m_materialBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pMaterialData));
    }

    {
        const UINT totalSize = Align256(sizeof(TessCB));
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_tessBuffer)));
        CD3DX12_RANGE rr(0, 0);
        m_tessBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pTessCbData));
    }

    {
        const UINT cbSize = Align256(sizeof(CB));
        const UINT totalSize = cbSize * static_cast<UINT>(m_objects.size()) * CSM_CASCADES;
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_shadowBuffer)));
        CD3DX12_RANGE rr(0, 0);
        m_shadowBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pShadowCbData));
    }

    {
        const UINT totalSize = Align256(sizeof(float) * 16);
        auto* device = m_framework->GetDevice();
        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_postBuffer)
        ));
        CD3DX12_RANGE rr(0, 0);
        m_postBuffer->Map(0, &rr, reinterpret_cast<void**>(&m_pPostData));
    }

    {
        m_previewCBStride = Align256(sizeof(PreviewCB));
        const UINT previewCount = 6;
        const UINT previewSize = m_previewCBStride * previewCount;

        CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(previewSize);
        ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_previewBuffer)
        ));
        m_previewBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pPreviewData));
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
    
    auto* allocator = m_framework->GetCommandAllocator();
    ThrowIfFailed(allocator->Reset());
    ID3D12GraphicsCommandList* initCmd = nullptr;
    ThrowIfFailed(m_framework->GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&initCmd)));

    m_gbuffer->Bind(initCmd);
    const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_gbuffer->Clear(initCmd, clearColor);
    m_gbuffer->TransitionToReadable(initCmd);

    ThrowIfFailed(initCmd->Close());
    ID3D12CommandList* initLists[] = { initCmd };
    m_framework->GetCommandQueue()->ExecuteCommandLists(1, initLists);
    m_framework->WaitForGpu();
    initCmd->Release();

    m_shadow = std::make_unique<ShadowMap>(m_framework, 2048 * 6, CSM_CASCADES);
    m_shadow->Initialize();

    auto* alloc = m_framework->GetCommandAllocator();

    alloc->Reset();
    cmd->Reset(alloc, nullptr);

    SetObjects();
    SetLights();

    RebuildOctree();

    ThrowIfFailed(cmd->Close());
    ID3D12CommandList* lists[] = { cmd };
    m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
    m_framework->WaitForGpu();

    LoadErrorTextures();
    LoadTextures();

    auto depthDesc = m_gbuffer->GetDepthResource()->GetDesc();
    m_depthWidth = static_cast<UINT>(depthDesc.Width);
    m_depthHeight = depthDesc.Height;

    D3D12_RESOURCE_DESC stagingDesc = CD3DX12_RESOURCE_DESC::Buffer(m_depthWidth * m_depthHeight * sizeof(float));
    auto* device = m_framework->GetDevice();
    CD3DX12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    ThrowIfFailed(device->CreateCommittedResource(
        &properties,
        D3D12_HEAP_FLAG_NONE,
        &stagingDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_depthStaging)));

    {
        using DirectX::ResourceUploadBatch;
        auto* device = m_framework->GetDevice();
        ResourceUploadBatch ub(device);
        ub.Begin();

        m_heightmapSrvIndex = loader.LoadTexture(m_framework->GetDevice(), ub, m_framework, L"Assets\\Terrain\\Mount\\Erosion2_Out.png");
        //m_heightmapSrvIndex = loader.LoadTexture(m_framework->GetDevice(), ub, m_framework, L"Assets\\Terrain\\height3.png");

        UINT terrainDiffuse = loader.LoadTexture(device, ub, m_framework, L"Assets\\Terrain\\Mount\\WaterColor_Out.png");
        UINT terrainNormal = loader.LoadTexture(device, ub, m_framework, L"Assets\\Terrain\\Hill\\Normals_Out.png");

        auto fut = ub.End(m_framework->GetCommandQueue());
        fut.wait();

        auto* alloc2 = m_framework->GetCommandAllocator();
        ThrowIfFailed(alloc2->Reset());
        ThrowIfFailed(cmd->Reset(alloc2, nullptr));

        m_terrain = std::make_unique<Terrain>(m_framework, &m_pipeline);
        m_terrain->SetRootAndPSO(m_pipeline.GetRootSignature(), m_pipeline.GetTerrainGBufferPSO());

        m_terrain->Initialize
        (
            m_heightmapSrvIndex,
            m_terrainWorldSize,
            m_terrainMaxDepth,
            m_terrainHeight,
            m_terrainSkirt,
            256
        );

        m_terrain->SetDiffuseTexture(terrainDiffuse);
        //m_terrain->SetNormalMap(terrainNormal, true);

        InitHeightDeltaTexture();
        m_terrain->SetHeightDeltaTexture(m_heightDeltaSrvIndex);

        offsetX = -(m_terrainWorldSize / 2);
        offsetZ = -(m_terrainWorldSize / 2);

        ThrowIfFailed(cmd->Close());
        ID3D12CommandList* lists2[] = { cmd };
        m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists2);
        m_framework->WaitForGpu();
    }

    {
        using DirectX::ResourceUploadBatch;
        auto* device = m_framework->GetDevice();
        ResourceUploadBatch ub(device);
        ub.Begin();

        m_ibl.irradianceSrv = loader.LoadDDSTextureCube(device, ub, m_framework, L"Assets\\IBL\\out\\RoomDiffuseHDR.dds");
        m_ibl.prefilteredSrv = loader.LoadDDSTextureCube(device, ub, m_framework, L"Assets\\IBL\\out\\RoomEnvHDR.dds");
        m_ibl.brdfSrv = loader.LoadTexture(device, ub, m_framework, L"Assets\\IBL\\out\\RoomBrdf.dds");

        auto fut = ub.End(m_framework->GetCommandQueue());
        fut.wait();

        m_ibl.tableStart = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
            m_ibl.irradianceSrv, m_framework->GetSrvDescriptorSize());
    }

    {
        auto* device = m_framework->GetDevice();
        const UINT width = static_cast<UINT>(m_framework->GetWidth());
        const UINT height = static_cast<UINT>(m_framework->GetHeight());

        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        D3D12_CLEAR_VALUE cv{}; cv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        cv.Color[0] = cv.Color[1] = cv.Color[2] = 0.0f; cv.Color[3] = 0.0f;

        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &cv, IID_PPV_ARGS(&m_lightAccum)
        ));

        auto* rtvHeap = m_framework->GetRtvHeap();
        const auto rtvHeapDesc = rtvHeap->GetDesc();
        const UINT rtvLastIndex = rtvHeapDesc.NumDescriptors - 1;

        m_lightAccumRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            rtvLastIndex, m_framework->GetRtvDescriptorSize());

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_framework->GetDevice()->CreateRenderTargetView(m_lightAccum.Get(), &rtvDesc, m_lightAccumRTV);

        m_lightAccumSrvIndex = m_framework->AllocateSrvDescriptor();
        auto srvCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
            m_lightAccumSrvIndex, m_framework->GetSrvDescriptorSize()
        );
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_lightAccum.Get(), &srvDesc, srvCPU);

        m_lightAccumSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
            m_lightAccumSrvIndex, m_framework->GetSrvDescriptorSize()
        );
    }

    {
        auto* device = m_framework->GetDevice();
        auto* rtvHeap = m_framework->GetRtvHeap();
        const UINT rtvInc = m_framework->GetRtvDescriptorSize();
        const auto rtvHeapDesc = rtvHeap->GetDesc();
        const UINT last = rtvHeapDesc.NumDescriptors - 1;

        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC ldrDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            (UINT)m_framework->GetWidth(),
            (UINT)m_framework->GetHeight(),
            1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        D3D12_CLEAR_VALUE ldrClear{};
        ldrClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ldrClear.Color[0] = ldrClear.Color[1] = ldrClear.Color[2] = 0.0f; ldrClear.Color[3] = 1.0f;

        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE, &ldrDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &ldrClear,
            IID_PPV_ARGS(&m_postA)));

        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE, &ldrDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &ldrClear,
            IID_PPV_ARGS(&m_postB)));

        m_postARTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            rtvHeap->GetCPUDescriptorHandleForHeapStart(), last - 2, rtvInc);
        m_postBRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            rtvHeap->GetCPUDescriptorHandleForHeapStart(), last - 1, rtvInc);

        device->CreateRenderTargetView(m_postA.Get(), nullptr, m_postARTV);
        device->CreateRenderTargetView(m_postB.Get(), nullptr, m_postBRTV);

        m_lightAccumRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            rtvHeap->GetCPUDescriptorHandleForHeapStart(), last, rtvInc);

        D3D12_RENDER_TARGET_VIEW_DESC hdrRtvDesc{};
        hdrRtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hdrRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(m_lightAccum.Get(), &hdrRtvDesc, m_lightAccumRTV);

        m_postASrvIndex = m_framework->AllocateSrvDescriptor();
        m_postBSrvIndex = m_framework->AllocateSrvDescriptor();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        auto  srvCPU0 = m_framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart();
        auto  srvGPU0 = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        UINT  srvInc = m_framework->GetSrvDescriptorSize();

        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuA(srvCPU0, m_postASrvIndex, srvInc);
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuB(srvCPU0, m_postBSrvIndex, srvInc);
        device->CreateShaderResourceView(m_postA.Get(), &srvDesc, cpuA);
        device->CreateShaderResourceView(m_postB.Get(), &srvDesc, cpuB);

        m_postASRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGPU0, m_postASrvIndex, srvInc);
        m_postBSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGPU0, m_postBSrvIndex, srvInc);
    }

    CreateConstantBuffers();

    {
        auto* device = m_framework->GetDevice();
        DirectX::ResourceUploadBatch upload(device);
        upload.Begin();

        m_shadowMaskSrvIndex = loader.LoadTexture(device, upload, m_framework, L"Assets\\TestShadows\\mask.png");

        auto finish = upload.End(m_framework->GetCommandQueue());
        finish.wait();

        auto  srvGPU0 = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        UINT  srvInc = m_framework->GetSrvDescriptorSize();
        m_shadowMaskSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGPU0, m_shadowMaskSrvIndex, srvInc);
    }

    m_particles = std::make_unique<ParticleSystem>(m_framework, &m_pipeline);
    UINT particles = 1;
    m_particles->Initialize(particles, particles);

    m_particles->EnableDepthCollisions
    (
        m_gbuffer->GetDepthResource(),
        (UINT)m_framework->GetWidth(),
        (UINT)m_framework->GetHeight()
    );
}

void RenderingSystem::Update(float)
{
    dt = timer.GetElapsedSeconds();

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

    m_backBufferState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    UpdateUI();

    BuildViewProj();

    XMMATRIX invVP = XMMatrixInverse(nullptr, viewProj);

    m_particles->UpdateViewProj(viewProj);

    m_particles->SetCameraMatrices(viewProj, invVP);
        
    ExtractVisibleObjects();
    UpdatePerObjectCBs();
    UpdateTessellationCB();

    ShadowPass();

    m_gbuffer->Bind(cmd);
    const float clearG[4] = { 0.2f, 0.2f, 1.0f, 0.0f };
    m_gbuffer->Clear(cmd, clearG);
    m_framework->SetViewportAndScissors();

    GeometryPass();

    UpdateTerrainBrush(cmd, dt);

    TerrainPass();

    {
        D3D12_RESOURCE_DESC depthDesc = m_gbuffer->GetDepthResource()->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT numRows;
        UINT64 rowSize;
        m_framework->GetDevice()->GetCopyableFootprints(&depthDesc, 0, 1, 0, &footprint, &numRows, &rowSize, nullptr);
        m_depthRowPitch = rowSize;

        auto toCopySrc = CD3DX12_RESOURCE_BARRIER::Transition(m_gbuffer->GetDepthResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd->ResourceBarrier(1, &toCopySrc);

        CD3DX12_TEXTURE_COPY_LOCATION src(m_gbuffer->GetDepthResource(), 0);
        CD3DX12_TEXTURE_COPY_LOCATION dst(m_depthStaging.Get(), footprint);
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        auto toDepthWrite = CD3DX12_RESOURCE_BARRIER::Transition(m_gbuffer->GetDepthResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmd->ResourceBarrier(1, &toDepthWrite);
    }

    m_gbuffer->TransitionToReadable(cmd);
    m_particles->Simulate(cmd, dt);

    m_gbuffer->Bind(cmd);
    m_framework->SetViewportAndScissors();
    m_particles->DrawGBuffer(cmd);

    m_gbuffer->TransitionToReadable(cmd);

    if (m_previewGBuffer)
    {
        PreviewGBufferPass();
    }
    else
    {
        DeferredPass();
        PostProcessPass();
    }

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);

    m_framework->EndFrame();

    m_transientUploads.clear();
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

        ImGui::Text("FPS: %.2f", m_currentFPS);
        ImGui::Text("Visible objects %d", m_visibleObjects.size());

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

    {
        ImGui::Begin("Post");

        ImGui::SliderFloat("Exposure", &postExposure, 0.1f, 4.0f);
        ImGui::InputFloat("Gamma", &postGamma, 0.1f);
        ImGui::SliderFloat("Vignette strength", &postVignetteStrength, 0.0f, 1.0f);
        ImGui::SliderFloat("Vignette power", &postVignettePower, 1.0f, 6.0f);
        ImGui::SliderFloat2("Vignette center", &postVignetteCenter.x, 0.0f, 1.0f);
        ImGui::RadioButton("Clamp", &postTonemap, 0); ImGui::SameLine();
        ImGui::RadioButton("Reinhard", &postTonemap, 1); ImGui::SameLine();
        ImGui::RadioButton("ACES", &postTonemap, 2);

        ImGui::Checkbox("Enable tonemap", &m_enableTonemap);
        ImGui::Checkbox("Enable gamma", &m_enableGamma);
        ImGui::Checkbox("Enable vignette", &m_enableVignette);

        ImGui::Checkbox("Invert", &m_enableInvert);
        ImGui::Checkbox("Grayscale", &m_enableGrayscale);
        ImGui::Checkbox("Pixelate", &m_enablePixelate);
        ImGui::SliderFloat("Pixelate Size", &pixelateSize, 1.0f, 64.0f);
        ImGui::Checkbox("Posterize", &m_enablePosterize);
        ImGui::SliderFloat("Posterize Levels", &posterizeLevels, 2.0f, 32.0f);
        ImGui::Checkbox("Saturation", &m_enableSaturation);
        ImGui::SliderFloat("Saturation Value", &saturation, 0.0f, 2.5f);

        ImGui::Checkbox("Preview GBuffer", &m_previewGBuffer);

        ImGui::End();
    }

    {
        ImGui::Begin("Terrain");

        ImGui::Text("Tiles %d", m_terrain->GetDrawTileCount());

        ImGui::SliderFloat("LOD screen tau", &m_screenTau, 0.0f, 1.0f);
        ImGui::SliderFloat("Height", &m_terrainHeight, 500.0f, 1500.0f);
        ImGui::SliderFloat("World size", &m_terrainWorldSize, 128.0f, 4096.0f);
        ImGui::SliderFloat("Offset X", &offsetX, -200.0f, 200.0f);
        ImGui::SliderFloat("Offset Z", &offsetZ, -200.0f, 200.0f);

        m_terrain->SetWorldParams({ offsetX, offsetZ }, m_terrainWorldSize);
        m_terrain->SetHeightScale(m_terrainHeight);

        ImGui::End();
    }

    {
        ImGui::Begin("Brush");

        ImGui::Checkbox("Enable brush", &m_brush.enabled);
        ImGui::SliderFloat("Radius", &m_brush.radiusWorld, 1.0f, 200.0f);
        ImGui::SliderFloat("Strength", &m_brush.strength, 0.01f, 1.0f);
        ImGui::SliderFloat("Hardness", &m_brush.hardness, 0.0f, 1.0f);

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
    if (!m_octree) return;

    std::vector<void*> hits;
    m_octree->QueryFrustum(planes, hits);

    for (void* p : hits)
    {
        m_visibleObjects.push_back(reinterpret_cast<SceneObject*>(p));
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

        mcb.hasDiffuseMap = obj->material.diffuseTexPath.empty() ? 0u : 1u;

        mcb.hasRoughMap = obj->material.roughnessTexPath.empty() ? 0u : 1u;
        mcb.hasMetalMap = obj->material.metallicTexPath.empty() ? 0u : 1u;
        mcb.hasAOMap = obj->material.aoTexPath.empty() ? 0u : 1u;

        mcb.roughnessValue = obj->material.roughness;
        mcb.metallicValue = obj->material.metallic;
        mcb.aoValue = obj->material.ao;

        mcb.baseColor = { obj->material.diffuse.x,
                          obj->material.diffuse.y,
                          obj->material.diffuse.z,
                          1.0f };

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
        cb.ScreenSize = { float(m_framework->GetWidth()), float(m_framework->GetHeight()), 0, 0 };
        for (UINT ci = 0; ci < CSM_CASCADES; ++ci) cb.LightViewProj[ci] = m_lightViewProjCSM[ci];
        cb.CascadeSplits = { m_cascadeSplits[0], m_cascadeSplits[1], m_cascadeSplits[2],
                             (CSM_CASCADES > 3 ? m_cascadeSplits[3] : m_cascadeSplits[2]) };
        XMStoreFloat4x4(&cb.View, view);
        cb.ShadowParams = { 1.0f / m_shadow->Size(), 0.001f, (float)m_shadow->CascadeCount(), 0 };

        cb.ShadowMaskParams = { m_shadowMaskTiling.x, m_shadowMaskTiling.y, m_shadowMaskStrength, 0.0f };

        cb.CameraPos = { cameraPos.x, cameraPos.y, cameraPos.z, 0.0f };

        memcpy(m_pLightData + UINT(i) * lightCBSize, &cb, sizeof(cb));
    }

    AmbientCB amb{};
    amb.AmbientColor = { 0.1f, 0.1f, 0.1f, 0.0f };
    memcpy(m_pAmbientData, &amb, sizeof(amb));
}

void RenderingSystem::UpdatePostCB()
{
    PostCB d{};

    d.Exposure = postExposure;
    d.Gamma = postGamma;
    d.VignetteStrength = postVignetteStrength;
    d.VignettePower = postVignettePower;
    d.VignetteCenter.x = postVignetteCenter.x;
    d.VignetteCenter.y = postVignetteCenter.y;
    d.InvResolution.x = 1.0f / m_framework->GetWidth();
    d.InvResolution.y = 1.0f / m_framework->GetHeight();
    d.Tonemap = postTonemap;
    d.Saturation = saturation;
    d.PixelateSize = pixelateSize;
    d.PosterizeLevels = posterizeLevels;

    memcpy(m_pPostData, &d, sizeof(d));
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

    for (size_t i = 0; i < m_visibleObjects.size(); ++i) 
    {
        SceneObject* obj = m_visibleObjects[i];

        const float dist = XMVectorGetX(XMVector3Length(XMLoadFloat3(&fakeCamPos) - XMLoadFloat3(&obj->position)));
        int lod = static_cast<int>(obj->lodDistances.size()) - 1;

        for (int j = 0; j + 1 < static_cast<int>(obj->lodDistances.size()); ++j) 
        {
            if (dist < obj->lodDistances[j + 1]) 
            {
                lod = j; break;
            }
        }

        if (!switchedToTransparent && obj->Color.w != 1.0f) 
        {
            cmd->SetPipelineState(m_pipeline.GetTransparentPSO());
            switchedToTransparent = true;
        }

        const bool useTess = (obj->texIdx[2] != errorTextures.height);
        if (useTess)
        {
            cmd->SetPipelineState(m_wireframe ? m_pipeline.GetGBufferTessellationWireframePSO() : m_pipeline.GetGBufferTessellationPSO());
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        }
        else 
        {
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
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_lightAccum.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        cmd->ResourceBarrier(1, &barrier);
    }

    cmd->OMSetRenderTargets(1, &m_lightAccumRTV, FALSE, nullptr);
    const float clearHDR[4] = { 0,0,0,0 };
    cmd->ClearRenderTargetView(m_lightAccumRTV, clearHDR, 0, nullptr);
    m_framework->SetViewportAndScissors();

    cmd->SetGraphicsRootSignature(m_pipeline.GetDeferredRS());

    SetCommonHeaps();
    UpdateLightCB();
    cmd->SetGraphicsRootDescriptorTable(0, m_gbuffer->GetSRVs()[0]);
    cmd->SetGraphicsRootConstantBufferView(1, m_lightBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootConstantBufferView(2, m_ambientBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(3, m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart());
    cmd->SetGraphicsRootDescriptorTable(4, m_shadow->Srv());
    cmd->SetGraphicsRootDescriptorTable(5, m_ibl.tableStart);
    cmd->SetGraphicsRootDescriptorTable(6, m_shadowMaskSRV);

    cmd->SetPipelineState(m_pipeline.GetSkyPSO());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    cmd->SetPipelineState(m_pipeline.GetAmbientPSO());
    cmd->DrawInstanced(3, 1, 0, 0);

    cmd->SetPipelineState(m_pipeline.GetDeferredPSO());
    const UINT lightCBSize = Align256(sizeof(LightCB));
    for (size_t i = 0; i < lights.size(); ++i) {
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_lightBuffer->GetGPUVirtualAddress() + static_cast<UINT>(i) * lightCBSize;
        cmd->SetGraphicsRootConstantBufferView(1, cbAddr);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_lightAccum.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        cmd->ResourceBarrier(1, &barrier);
    }
}

void RenderingSystem::BuildLightViewProjCSM()
{
    const float n = m_near;
    const float f = m_far;
    const float lambda = 0.5f;
    float splits[CSM_CASCADES];
    for (UINT i = 0; i < CSM_CASCADES; ++i)
    {
        float p = (i + 1) / float(CSM_CASCADES);
        float logS = n * powf(f / n, p);
        float uniS = n + (f - n) * p;
        splits[i] = (1.0f - lambda) * uniS + lambda * logS;
        m_cascadeSplits[i] = splits[i];
    }

    XMVECTOR L = XMVector3Normalize(XMLoadFloat3(&direction));
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    const float dotY = XMVectorGetX(XMVector3Dot(L, up));
    if (fabsf(dotY) > 0.99f) up = XMVectorSet(0, 0, 1, 0);

    const float aspect = static_cast<float>(m_framework->GetWidth()) / m_framework->GetHeight();
    const float fov = XM_PIDIV4;
    XMMATRIX V = view;

    const float overlapRatio = 0.15f;
    const float receiverPadXY = 100.0f;
    const float receiverPadZ = 200.0f;
    const float minCasterExt = 2000.0f;

    for (UINT ci = 0; ci < CSM_CASCADES; ++ci)
    {
        float splitNear = (ci == 0 ? n : splits[ci - 1]);
        float splitFar = splits[ci];

        float range = (splitFar - splitNear);
        float zOverlap = overlapRatio * range;
        float splitNearOver = max(n, splitNear - zOverlap);
        float splitFarOver = min(f, splitFar + zOverlap);

        XMMATRIX Psplit = XMMatrixPerspectiveFovLH(fov, aspect, splitNearOver, splitFarOver);
        XMMATRIX invVP = XMMatrixInverse(nullptr, V * Psplit);

        XMVECTOR clip[8] = {
            XMVectorSet(-1,-1,0,1), XMVectorSet(1,-1,0,1),
            XMVectorSet(1, 1,0,1), XMVectorSet(-1, 1,0,1),
            XMVectorSet(-1,-1,1,1), XMVectorSet(1,-1,1,1),
            XMVectorSet(1, 1,1,1), XMVectorSet(-1, 1,1,1),
        };
        XMVECTOR cornersWS[8];
        XMVECTOR center = XMVectorZero();
        for (int i = 0; i < 8; ++i) {
            XMVECTOR v = XMVector4Transform(clip[i], invVP);
            v = XMVectorScale(v, 1.0f / XMVectorGetW(v));
            cornersWS[i] = v;
            center = XMVectorAdd(center, v);
        }
        center = XMVectorScale(center, 1.0f / 8.0f);

        const float distBack = splitFarOver;
        XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(L, distBack));
        XMMATRIX LV = XMMatrixLookAtLH(eye, center, up);

        XMVECTOR minv = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
        XMVECTOR maxv = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);
        for (int i = 0; i < 8; ++i) {
            XMVECTOR p = XMVector3TransformCoord(cornersWS[i], LV);
            minv = XMVectorMin(minv, p);
            maxv = XMVectorMax(maxv, p);
        }
        float minX = XMVectorGetX(minv), maxX = XMVectorGetX(maxv);
        float minY = XMVectorGetY(minv), maxY = XMVectorGetY(maxv);
        float minZ = XMVectorGetZ(minv), maxZ = XMVectorGetZ(maxv);

        float w = (maxX - minX);
        float h = (maxY - minY);

        float worldUnitsPerTexel = max(w, h) / m_shadow->Size();
        float cx = 0.5f * (minX + maxX);
        float cy = 0.5f * (minY + maxY);
        cx = floorf(cx / worldUnitsPerTexel) * worldUnitsPerTexel;
        cy = floorf(cy / worldUnitsPerTexel) * worldUnitsPerTexel;
        float hx = 0.5f * w, hy = 0.5f * h;
        minX = cx - hx; maxX = cx + hx;
        minY = cy - hy; maxY = cy + hy;

        minX -= receiverPadXY; maxX += receiverPadXY;
        minY -= receiverPadXY; maxY += receiverPadXY;

        const float casterExt = max(minCasterExt, 4.0f * range);
        const float recvPadZ = receiverPadZ;

        minZ = max(0.1f, minZ - casterExt);
        maxZ = maxZ + recvPadZ;

        XMMATRIX LP = XMMatrixOrthographicOffCenterLH(minX, maxX, minY, maxY, minZ, maxZ);
        XMMATRIX LVP = LV * LP;

        XMStoreFloat4x4(&m_lightViewProjCSM[ci], LVP);

        ExtractShadowCastersForCascade(ci, LV, minX, maxX, minY, maxY, minZ, maxZ);
    }
}

void RenderingSystem::ShadowPass()
{
    BuildLightViewProjCSM();

    auto* cl = m_framework->GetCommandList();

    auto toWrite = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadow->Resource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );
    cl->ResourceBarrier(1, &toWrite);

    auto vp = m_shadow->GetViewport();
    auto sc = m_shadow->GetScissor();
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);

    cl->SetGraphicsRootSignature(m_pipeline.GetRootSignature());
    SetCommonHeaps();
    cl->SetPipelineState(m_pipeline.GetShadowPSO());
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const UINT cbSize = Align256(sizeof(CB));
    const UINT perCascadeCapacity = static_cast<UINT>(m_objects.size());

    for (UINT ci = 0; ci < CSM_CASCADES; ++ci)
    {
        auto dsv = m_shadow->Dsv(ci);
        cl->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        const UINT base = ci * perCascadeCapacity;

        for (size_t i = 0; i < m_shadowCasters[ci].size(); ++i)
        {
            SceneObject* obj = m_shadowCasters[ci][i];

            CB cb{};
            XMStoreFloat4x4(&cb.World, obj->GetWorldMatrix());
            cb.ViewProj = m_lightViewProjCSM[ci];

            const UINT slot = base + static_cast<UINT>(i);
            memcpy(m_pShadowCbData + slot * cbSize, &cb, sizeof(cb));
            cl->SetGraphicsRootConstantBufferView(
                0, m_shadowBuffer->GetGPUVirtualAddress() + slot * cbSize);

            int lod = (int)obj->lodMeshes.size() - 1;
            cl->IASetVertexBuffers(0, 1, &obj->lodVBs[lod]);
            cl->IASetIndexBuffer(&obj->lodIBs[lod]);
            cl->DrawIndexedInstanced((UINT)obj->lodMeshes[lod].indices.size(), 1, 0, 0, 0);
        }
    }

    auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadow->Resource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cl->ResourceBarrier(1, &toRead);
}

void RenderingSystem::ExtractShadowCastersForCascade(UINT ci, const XMMATRIX& LV, float minX, float maxX, float minY, float maxY, float minZ, float maxZ)
{
    m_shadowCasters[ci].clear();
    m_shadowCasters[ci].reserve(m_objects.size());

    XMVECTOR bmin = XMVectorSet(minX, minY, minZ, 0);
    XMVECTOR bmax = XMVectorSet(maxX, maxY, maxZ, 0);

    for (auto& o : m_objects)
    {
        const XMMATRIX world = o.GetWorldMatrix();
        XMVECTOR cW = XMVector3Transform(XMLoadFloat3(&o.bsCenter), world);

        const auto& s = o.scale;
        float scaleLen = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
        float rW = o.bsRadius * scaleLen;

        XMVECTOR cL = XMVector3TransformCoord(cW, LV);

        XMVECTOR minS = XMVectorSet(XMVectorGetX(cL) - rW, XMVectorGetY(cL) - rW, XMVectorGetZ(cL) - rW, 0);
        XMVECTOR maxS = XMVectorSet(XMVectorGetX(cL) + rW, XMVectorGetY(cL) + rW, XMVectorGetZ(cL) + rW, 0);

        XMVECTOR overMin = XMVectorGreaterOrEqual(maxS, bmin);
        XMVECTOR underMax = XMVectorLessOrEqual(minS, bmax);
        if (XMVector3EqualInt(XMVectorAndInt(overMin, underMax), XMVectorTrueInt()))
            m_shadowCasters[ci].push_back(&o);
    }
}

void RenderingSystem::ComputeLocalSphereFromMesh(const Mesh& m, XMFLOAT3& c, float& r)
{
    using namespace DirectX;

    XMVECTOR vmin = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
    XMVECTOR vmax = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);

    for (auto& v : m.vertices)
    {
        XMVECTOR p = XMLoadFloat3(&v.Pos);
        vmin = XMVectorMin(vmin, p); vmax = XMVectorMax(vmax, p);
    }

    XMVECTOR center = 0.5f * (vmin + vmax);
    XMVECTOR half = 0.5f * (vmax - vmin);
    r = XMVectorGetX(XMVector3Length(half));
    XMStoreFloat3(&c, center);
}

AABB RenderingSystem::MakeWorldAABBFromSphere(const XMMATRIX& world, const XMFLOAT3& cLocal, float rLocal, const XMFLOAT3& scale)
{
    using namespace DirectX;

    XMVECTOR cL = XMLoadFloat3(&cLocal);
    XMVECTOR cw = XMVector3Transform(cL, world);
    float scaleLen = std::sqrt(scale.x * scale.x + scale.y * scale.y + scale.z * scale.z);
    float rW = rLocal * scaleLen;
    XMFLOAT3 cW; XMStoreFloat3(&cW, cw);
    AABB a{};
    a.minv = { cW.x - rW, cW.y - rW, cW.z - rW };
    a.maxv = { cW.x + rW, cW.y + rW, cW.z + rW };
    return a;
}

void RenderingSystem::RebuildOctree()
{
    if (m_objects.empty())
    {
        m_octree.reset(); return;
    }

    AABB scene{};
    std::vector<OctItem> items; items.reserve(m_objects.size());

    for (auto& o : m_objects)
    {
        const Mesh& m = (!o.lodMeshes.empty() ? o.lodMeshes.front() : o.mesh);
        XMFLOAT3 cL{}; float rL = 0.0f;
        ComputeLocalSphereFromMesh(m, cL, rL);

        const XMMATRIX W = o.GetWorldMatrix();
        AABB a = MakeWorldAABBFromSphere(W, cL, rL, o.scale);

        scene.expand(a);
        items.push_back({ a, &o });
    }

    const float pad = 1.0f;
    scene.minv.x -= pad; scene.minv.y -= pad; scene.minv.z -= pad;
    scene.maxv.x += pad; scene.maxv.y += pad; scene.maxv.z += pad;

    if (!m_octree) m_octree = std::make_unique<Octree>();
    m_octree->Build(scene, items, 8, 8, 2.0f);
}

void RenderingSystem::PostProcessPass()
{
    cmd->SetGraphicsRootSignature(m_pipeline.GetPostRS());
    SetCommonHeaps();
    UpdatePostCB();

    D3D12_GPU_DESCRIPTOR_HANDLE cur = m_lightAccumSRV;
    bool useA = true;

    auto takeDstRes = [&](bool a)->ID3D12Resource* { return a ? m_postA.Get() : m_postB.Get(); };
    auto takeDstRTV = [&](bool a)->D3D12_CPU_DESCRIPTOR_HANDLE { return a ? m_postARTV : m_postBRTV;   };

    auto doInter = [&](ID3D12PipelineState* pso)
        {
            auto* dstRes = takeDstRes(useA);
            auto  dstRTV = takeDstRTV(useA);
            D3D12_GPU_DESCRIPTOR_HANDLE outSrv{};
            ApplyPassToIntermediate(pso, cur, dstRes, dstRTV, outSrv);
            cur = outSrv;
            useA = !useA;
        };

    std::vector<ID3D12PipelineState*> passes;
    passes.push_back(m_enableTonemap ? m_pipeline.GetTonemapPSO()
        : m_pipeline.GetCopyHDRtoLDRPSO());

    passes.push_back(m_enableGamma ? m_pipeline.GetGammaPSO()
        : m_pipeline.GetCopyLDRPSO());

    if (m_enableInvert) passes.push_back(m_pipeline.GetInvertPSO());
    if (m_enableGrayscale) passes.push_back(m_pipeline.GetGrayscalePSO());
    if (m_enablePixelate) passes.push_back(m_pipeline.GetPixelatePSO());
    if (m_enablePosterize) passes.push_back(m_pipeline.GetPosterizePSO());
    if (m_enableSaturation) passes.push_back(m_pipeline.GetSaturationPSO());

    passes.push_back(m_enableVignette ? m_pipeline.GetVignettePSO()
        : m_pipeline.GetCopyLDRPSO());

    for (size_t i = 0; i < passes.size(); ++i)
    {
        bool last = (i + 1 == passes.size());
        if (last) ApplyPassToBackbuffer(passes[i], cur);
        else      doInter(passes[i]);
    }
}

void RenderingSystem::ApplyPassToIntermediate( ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE inSrv, ID3D12Resource* dst, D3D12_CPU_DESCRIPTOR_HANDLE dstRtv, D3D12_GPU_DESCRIPTOR_HANDLE& outSrv)
{
    auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(
        dst, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &toRT);

    cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
    const float clear[4] = { 0,0,0,1 };
    cmd->ClearRenderTargetView(dstRtv, clear, 0, nullptr);
    m_framework->SetViewportAndScissors();

    cmd->SetPipelineState(pso);
    cmd->SetGraphicsRootDescriptorTable(0, inSrv);
    cmd->SetGraphicsRootConstantBufferView(1, m_postBuffer->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
        dst, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &toSRV);

    if (dst == m_postA.Get()) 
    { 
        outSrv = m_postASRV;
    }
    else
    { 
        outSrv = m_postBSRV;
    }
}

void RenderingSystem::ApplyPassToBackbuffer(ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE inSrv)
{
    auto bbRtv = m_framework->GetCurrentRTVHandle();
    cmd->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);

    const float clear[4] = { 0, 0, 0, 1 };
    cmd->ClearRenderTargetView(bbRtv, clear, 0, nullptr);
    m_framework->SetViewportAndScissors();

    cmd->SetPipelineState(pso);
    cmd->SetGraphicsRootDescriptorTable(0, inSrv);
    cmd->SetGraphicsRootConstantBufferView(1, m_postBuffer->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}

void RenderingSystem::PreviewGBufferPass()
{
    auto bbRtv = m_framework->GetCurrentRTVHandle();
    const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmd->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
    cmd->ClearRenderTargetView(bbRtv, clear, 0, nullptr);

    auto srvs = m_gbuffer->GetSRVs();

    cmd->SetGraphicsRootSignature(m_pipeline.GetPreviewRS());
    cmd->SetPipelineState(m_pipeline.GetPreviewPSO());
    SetCommonHeaps();

    const float width = m_framework->GetWidth();
    const float height = m_framework->GetHeight();
    const float cellW = width / 3.0f;
    const float cellH = height / 2.0f;

    struct GridItem 
    { 
        int mode; 
        float x, y;
    };

    GridItem grid[] =
    {
        {0, 0.0f, 0.0f},  
        {1, cellW, 0.0f},  
        {2, cellW * 2.0f, 0.0f},
        {3, 0.0f, cellH}, 
        {4, cellW, cellH}, 
        {5, cellW * 2.0f, cellH}
    };

    PreviewCB pcb{};
    pcb.nearPlane = m_near;
    pcb.farPlane = 100.0f;

    int i = 0;
    for (auto& item : grid)
    {
        pcb.mode = item.mode;

        uint8_t* dst = m_pPreviewData + i * m_previewCBStride;
        memcpy(dst, &pcb, sizeof(PreviewCB));

        D3D12_VIEWPORT vp{ item.x, item.y, cellW, cellH, 0.0f, 1.0f };
        D3D12_RECT sc{ (LONG)item.x, (LONG)item.y, (LONG)(item.x + cellW), (LONG)(item.y + cellH) };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);

        cmd->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);

        D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = m_previewBuffer->GetGPUVirtualAddress() + static_cast<UINT64>(i) * m_previewCBStride;
        cmd->SetGraphicsRootConstantBufferView(0, gpuAddr);

        cmd->SetGraphicsRootDescriptorTable(1, srvs[0]);

        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);

        ++i;
    }
}

void RenderingSystem::TerrainPass()
{
    XMFLOAT4 planes[6];
    ExtractFrustumPlanes(planes, viewProj);

    XMFLOAT4X4 vp;
    XMStoreFloat4x4(&vp, viewProj);

    const float pxThreshold = 1024.0f;
    const float width = static_cast<float>(m_framework->GetWidth());
    const float screenTauNDC = (pxThreshold / width) * 2.0f;
    //m_terrain->Collect(cameraPos, planes, vp, screenTauNDC);
    
    m_terrain->Collect(cameraPos, planes, vp, m_screenTau);

    cmd->SetGraphicsRootSignature(m_pipeline.GetRootSignature());
    SetCommonHeaps();

    auto srvStart = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
    cmd->SetGraphicsRootDescriptorTable(3, srvStart);

    auto sampStart = m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart();
    cmd->SetGraphicsRootDescriptorTable(4, sampStart);

    m_terrain->DrawGBuffer(cmd);
}

void RenderingSystem::InitHeightDeltaTexture()
{
    auto* device = m_framework->GetDevice();
    auto* queue = m_framework->GetCommandQueue();

    m_heightDeltaCPU.assign(m_heightDeltaW * m_heightDeltaH, 0.0f);

    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_FLOAT,
        (UINT)m_heightDeltaW, (UINT)m_heightDeltaH,
        1, 1, 1, 0);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_heightDeltaTex)));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0; UINT64 rowSize = 0, total = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &fp, &rows, &rowSize, &total);

    ComPtr<ID3D12Resource> uploadBuffer;
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC   bufDesc = CD3DX12_RESOURCE_DESC::Buffer(total);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    void* p = nullptr;
    ThrowIfFailed(uploadBuffer->Map(0, nullptr, &p));
    for (UINT y = 0; y < rows; ++y)
        memset((uint8_t*)p + fp.Offset + y * fp.Footprint.RowPitch, 0, (size_t)rowSize);
    uploadBuffer->Unmap(0, nullptr);

    ComPtr<ID3D12CommandAllocator> tempAlloc;
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempAlloc)));

    ComPtr<ID3D12GraphicsCommandList> tempCmd;
    ThrowIfFailed(device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, tempAlloc.Get(), nullptr,
        IID_PPV_ARGS(&tempCmd)));

    CD3DX12_TEXTURE_COPY_LOCATION dst(m_heightDeltaTex.Get(), 0);
    CD3DX12_TEXTURE_COPY_LOCATION src(uploadBuffer.Get(), fp);
    tempCmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
        m_heightDeltaTex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    tempCmd->ResourceBarrier(1, &toSRV);

    ThrowIfFailed(tempCmd->Close());
    ID3D12CommandList* lists[] = { tempCmd.Get() };
    queue->ExecuteCommandLists(1, lists);
    m_framework->WaitForGpu();

    m_heightDeltaSrvIndex = m_framework->AllocateSrvDescriptor();
    auto cpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
        m_heightDeltaSrvIndex, m_framework->GetSrvDescriptorSize());

    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_heightDeltaTex.Get(), &sd, cpu);
}

bool RenderingSystem::ScreenToWorldRay(float mx, float my, XMVECTOR& ro, XMVECTOR& rd)
{
    float w = (float)m_framework->GetWidth();
    float h = (float)m_framework->GetHeight();
    float x = 2.f * mx / w - 1.f;
    float y = 1.f - 2.f * my / h;
    XMMATRIX invVP = XMMatrixInverse(nullptr, viewProj);

    XMVECTOR pN = XMVectorSet(x, y, 0.f, 1.f);
    XMVECTOR pF = XMVectorSet(x, y, 1.f, 1.f);
    pN = XMVector4Transform(pN, invVP);
    pF = XMVector4Transform(pF, invVP);
    pN = XMVectorScale(pN, 1.f / XMVectorGetW(pN));
    pF = XMVectorScale(pF, 1.f / XMVectorGetW(pF));

    ro = pN;
    rd = XMVector3Normalize(pF - pN);
    return true;
}

bool RenderingSystem::RayPlaneY0(const XMVECTOR& ro, const XMVECTOR& rd, XMFLOAT3& hit)
{
    float dy = XMVectorGetY(rd);
    if (fabsf(dy) < 1e-6f) return false;
    float t = -XMVectorGetY(ro) / dy;
    if (t <= 0.f) return false;
    XMVECTOR p = ro + t * rd;
    XMStoreFloat3(&hit, p);
    return true;
}

bool RenderingSystem::WorldToTerrainUV(const XMFLOAT3& P, XMFLOAT2& uv)
{
    float u = (P.x - offsetX) / m_terrainWorldSize;
    float v = (P.z - offsetZ) / m_terrainWorldSize;
    if (u < 0.f || u > 1.f || v < 0.f || v > 1.f) return false;
    uv = { u, v };
    return true;
}

void RenderingSystem::UpdateTerrainBrush(ID3D12GraphicsCommandList* cmd, float dt)
{

    if (!m_brush.enabled) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    m_brush.painting = io.MouseDown[0];

    if (!m_brush.painting) return;

    m_brush.invert = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    POINT mouse;
    GetCursorPos(&mouse);
    ScreenToClient(m_framework->GetHwnd(), &mouse);
    int mx = std::clamp(static_cast<int>(mouse.x), 0, static_cast<int>(m_depthWidth) - 1);
    int my = std::clamp(static_cast<int>(mouse.y), 0, static_cast<int>(m_depthHeight) - 1);

    void* mappedData = nullptr;
    m_depthStaging->Map(0, nullptr, &mappedData);
    float* depthData = static_cast<float*>(mappedData);
    UINT colPitch = static_cast<UINT>(m_depthRowPitch / sizeof(float));
    float depth = depthData[my * colPitch + mx];
    m_depthStaging->Unmap(0, nullptr);

    if (depth >= 1.0f || depth <= 0.0f) return;

    float ndcX = (2.0f * mx / m_depthWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / m_depthHeight);
    XMFLOAT4 clipPos(ndcX, ndcY, depth, 1.0f);

    XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj); 
    XMVECTOR worldVec = XMVector4Transform(XMLoadFloat4(&clipPos), invViewProj);
    worldVec = XMVectorScale(worldVec, 1.0f / XMVectorGetW(worldVec));
    XMFLOAT3 worldHit;
    XMStoreFloat3(&worldHit, worldVec);

    XMFLOAT2 uv;
    if (!WorldToTerrainUV(worldHit, uv)) return;

    ApplyBrushAtUV(uv, dt, cmd);
}

void RenderingSystem::ApplyBrushAtUV(const XMFLOAT2& uv, float dt, ID3D12GraphicsCommandList* cmd)
{
    const float sign = m_brush.invert ? -1.f : 1.f;

    float rUV = m_brush.radiusWorld / m_terrainWorldSize;
    int   rPx = (int)ceilf(rUV * m_heightDeltaW);

    int cx = (int)floorf(uv.x * m_heightDeltaW);
    int cy = (int)floorf(uv.y * m_heightDeltaH);
    int x0 = max(0, cx - rPx), x1 = min(m_heightDeltaW - 1, cx + rPx);
    int y0 = max(0, cy - rPx), y1 = min(m_heightDeltaH - 1, cy + rPx);

    if (x0 > x1 || y0 > y1) return;

    float expH = 1.0f + m_brush.hardness * 8.0f;
    float k = sign * m_brush.strength * dt;

    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            float dx = (x + 0.5f - cx), dy = (y + 0.5f - cy);
            float d = sqrtf(dx * dx + dy * dy);
            if (d > rPx) continue;

            float t = 1.0f - (d / (float)rPx);
            float w = powf(max(0.0f, t), expH);

            float& cell = m_heightDeltaCPU[y * m_heightDeltaW + x];
            cell = std::clamp(cell + k * w, -1.0f, 1.0f);
        }
    }

    UploadRegionToGPU(x0, y0, x1 - x0 + 1, y1 - y0 + 1, cmd);
}

void RenderingSystem::UploadRegionToGPU(int x0, int y0, int w, int h, ID3D12GraphicsCommandList* cmd)
{
    if (w <= 0 || h <= 0) return;

    auto* device = m_framework->GetDevice();

    CD3DX12_RESOURCE_DESC regionTexDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, (UINT)w, (UINT)h, 1, 1);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0; UINT64 rowSize = 0, total = 0;
    device->GetCopyableFootprints(&regionTexDesc, 0, 1, 0, &fp, &rows, &rowSize, &total);

    ComPtr<ID3D12Resource> uploadBuffer;
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC   bufDesc = CD3DX12_RESOURCE_DESC::Buffer(total);

    ThrowIfFailed(device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

    m_transientUploads.push_back(uploadBuffer);

    void* p = nullptr; ThrowIfFailed(uploadBuffer->Map(0, nullptr, &p));
    uint8_t* dstBase = (uint8_t*)p + fp.Offset;

    for (UINT row = 0; row < rows; ++row)
    {
        const float* src = &m_heightDeltaCPU[(y0 + (int)row) * m_heightDeltaW + x0];
        memcpy(dstBase + row * fp.Footprint.RowPitch, src, (size_t)rowSize);
    }
    uploadBuffer->Unmap(0, nullptr);

    CD3DX12_RESOURCE_BARRIER toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        m_heightDeltaTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->ResourceBarrier(1, &toCopy);

    CD3DX12_TEXTURE_COPY_LOCATION dst(m_heightDeltaTex.Get(), 0); 
    CD3DX12_TEXTURE_COPY_LOCATION src(uploadBuffer.Get(), fp);    

    D3D12_BOX srcBox{ 0u, 0u, 0u, (UINT)w, (UINT)h, 1u };
    cmd->CopyTextureRegion(&dst, (UINT)x0, (UINT)y0, 0, &src, &srcBox);

    CD3DX12_RESOURCE_BARRIER toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
        m_heightDeltaTex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &toSRV);
}
