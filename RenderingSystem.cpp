#include "RenderingSystem.h"
#include "AssetLoader.h"
#include "FrustumPlane.h"
#include <filesystem>
#include <iostream>
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "ShadowMap.h"
#include "Meshlets.h"

using namespace DirectX;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr)) throw std::runtime_error("HRESULT failed");
}

struct CB 
{
    XMFLOAT4X4 World;
    XMFLOAT4X4 ViewProj;
};

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

    UINT FrameIndex;
    XMFLOAT3 _padFrame;
};

struct AmbientCB 
{
    XMFLOAT4 AmbientColor;

    XMFLOAT4 FogColorDensity;

    XMFLOAT4 FogParams;

    XMFLOAT4 SunParams;
};

struct TessCB
{
    XMFLOAT3 cameraPos; float heightScale;
    float minDist; float maxDist;
    float minTess; float maxTess;
};

struct MaterialCB
{
    float      useNormalMap;
    UINT       diffuseIdx;
    UINT       normalIdx;
    UINT       dispIdx;

    UINT       roughIdx;
    UINT       metalIdx;
    UINT       aoIdx;
    UINT       hasDiffuseMap;

    XMFLOAT4   baseColor;

    UINT       useRoughMap;
    UINT       useMetalMap;
    UINT       useAOMap;
    UINT       hasRoughMap;

    UINT       hasMetalMap;
    UINT       hasAOMap;
    UINT       _padM0;
    UINT       _padM1;
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

struct ErrorTextures 
{
    UINT white{};
    UINT roughness{};
    UINT metallic{};
    UINT normal{};
    UINT height{};
    UINT ambientOcclusion{};
    UINT diffuse{};
} errorTextures;

struct TAACB
{
    XMFLOAT2 jitterCur;
    XMFLOAT2 jitterPrev;
    float  alpha;
    float _pad0;
    XMFLOAT2 invResolution;
    float  clampK;
    float reactiveK;

    XMFLOAT4X4 CurrViewProjInv;
    XMFLOAT4X4 PrevViewProj;

    float zDiffNdc;
    float uvGuard;
    float _padA;
    float _padB;
};

struct VelCBData
{
    XMFLOAT2 invRes;
    XMFLOAT2 jitterCur;
    XMFLOAT2 jitterPrev;
    XMFLOAT4X4 CurrInv;
    XMFLOAT4X4 PrevVP;
    float uvGuard;
    float zDiffNdc;
    float pad0, pad1;
};

struct AlphaShadowCBData
{
    uint32_t GrassInstanceID;
    uint32_t GrassUsesXZ;
    float GrassAlphaCutoff;
    float _pad0;

    XMFLOAT2 GrassUvScale;
    XMFLOAT2 GrassUvOffset;
};

static float Halton(uint32_t index, uint32_t base)
{
    float f = 1.0f;
    float result = 0.0f;

    while (index > 0) 
    { 
        f /= base;
        result += f * (index % base);
        index /= base; 
    }

    return result;
}

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
            //"Assets\\TestPBR\\TestPBR.obj", 
            //"Assets\\Can\\Gas_can.obj", 
            //"Assets\\LOD\\bunnyLOD0.obj", 
            //"Assets\\LOD\\bunnyLOD1.obj", 
            //"Assets\\LOD\\bunnyLOD2.obj", 
            //"Assets\\LOD\\bunnyLOD3.obj", 
            //"Assets\\TestShadows\\test.obj", 
            //"Assets\\TestShadows\\floor.obj", 
            "Assets\\TestShadows\\TestRT.obj", 
            //"Assets\\Cube\\cube.obj", 
            //"Assets\\Camera\\vintage_video_camera_1k.obj",
        },
        { 0.0f, 500.0f, 1000.0f, 1500.0f, }
    );

    m_objectScale = 1.1f;
    for (auto& obj : m_objects) obj.scale = { m_objectScale, m_objectScale, m_objectScale };

    m_meshletData.clear();
    m_meshletData.resize(m_objects.size());
    m_meshletUploads.clear();

    for (size_t objIndex = 0; objIndex < m_objects.size(); ++objIndex)
    {
        auto& obj = m_objects[objIndex];

        const size_t L = obj.lodMeshes.size();
        obj.lodVertexBuffers.resize(L);
        obj.lodVertexUploads.resize(L);
        obj.lodVBs.resize(L);
        obj.lodIndexBuffers.resize(L);
        obj.lodIndexUploads.resize(L);
        obj.lodIBs.resize(L);

        m_meshletData[objIndex].resize(L);

        for (size_t i = 0; i < L; ++i) 
        {
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

            if (m_framework->IsMeshShaderSupported())
            {
                MeshletDrawData& md = m_meshletData[objIndex][i];

                std::vector<uint32_t> idx32;
                idx32.reserve(obj.lodMeshes[i].indices.size());
                for (auto v : obj.lodMeshes[i].indices) idx32.push_back((uint32_t)v);

                std::vector<Meshlet> meshlets;
                std::vector<uint32_t> meshletVerts;
                std::vector<uint32_t> meshletPrims;

                BuildMeshlets_Greedy(idx32, 64, 126, meshlets, meshletVerts, meshletPrims);

                md.meshletCount = (uint32_t)meshlets.size();
                if (md.meshletCount != 0)
                {
                    if (obj.lodVBs[i].StrideInBytes != sizeof(MeshVertex))
                        throw std::runtime_error("vertex stride mismatch");

                    ComPtr<ID3D12Resource> up0, up1, up2;

                    m_framework->CreateDefaultBuffer(cmd, meshlets.data(),
                        (UINT64)meshlets.size() * sizeof(Meshlet), md.meshlets, up0);

                    m_framework->CreateDefaultBuffer(cmd, meshletVerts.data(),
                        (UINT64)meshletVerts.size() * sizeof(uint32_t), md.meshletVertices, up1);

                    m_framework->CreateDefaultBuffer(cmd, meshletPrims.data(),
                        (UINT64)meshletPrims.size() * sizeof(uint32_t), md.meshletPrims, up2);

                    md.srvBase = m_framework->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4);

                    CreateMeshletSRVs(
                        m_framework->GetDevice(),
                        obj.lodVertexBuffers[i].Get(),
                        md.meshlets.Get(),
                        md.meshletVertices.Get(),
                        md.meshletPrims.Get(),
                        m_framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
                        m_framework->GetSrvDescriptorSize(),
                        md.srvBase
                    );

                    m_meshletUploads.push_back(up0);
                    m_meshletUploads.push_back(up1);
                    m_meshletUploads.push_back(up2);
                }
            }

        }
    }

    BuildRaytracingAS();
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
    //std::filesystem::path sceneFolder = L"Assets\\TestPBR";
    //std::filesystem::path sceneFolder = L"Assets\\Can";
    //std::filesystem::path sceneFolder = L"Assets\\LOD";
    std::filesystem::path sceneFolder = L"Assets\\TestShadows";
    //std::filesystem::path sceneFolder = L"Assets\\Cube";
    //std::filesystem::path sceneFolder = L"Assets\\Camera";

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

    {
        const UINT totalSize = Align256(sizeof(TAACB));
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_taaCB)));
        CD3DX12_RANGE rr(0, 0);
        m_taaCB->Map(0, &rr, reinterpret_cast<void**>(&m_pTaaData));
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

    InitAlphaShadowDemoResources();

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

        D3D12_DESCRIPTOR_HEAP_DESC dLA = {};
        dLA.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        dLA.NumDescriptors = 1;
        ThrowIfFailed(device->CreateDescriptorHeap(&dLA, IID_PPV_ARGS(&m_lightAccumRTVHeap)));

        m_lightAccumRTV = m_lightAccumRTVHeap->GetCPUDescriptorHandleForHeapStart();

        D3D12_RENDER_TARGET_VIEW_DESC rtvLA{};
        rtvLA.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rtvLA.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(m_lightAccum.Get(), &rtvLA, m_lightAccumRTV);

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
            D3D12_RESOURCE_STATE_RENDER_TARGET, 
            &ldrClear,
            IID_PPV_ARGS(&m_postA)));

        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE, &ldrDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &ldrClear,
            IID_PPV_ARGS(&m_postB)));

        m_postARTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            rtvHeap->GetCPUDescriptorHandleForHeapStart(), last - 2, rtvInc);
        m_postBRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            rtvHeap->GetCPUDescriptorHandleForHeapStart(), last - 1, rtvInc);

        device->CreateRenderTargetView(m_postA.Get(), nullptr, m_postARTV);
        device->CreateRenderTargetView(m_postB.Get(), nullptr, m_postBRTV);

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

        m_postAState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_postBState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    {
        auto* device = m_framework->GetDevice();
        const UINT w = (UINT)m_framework->GetWidth();
        const UINT h = (UINT)m_framework->GetHeight();

        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R16G16_FLOAT, w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        D3D12_CLEAR_VALUE cv{};
        cv.Format = DXGI_FORMAT_R16G16_FLOAT;
        cv.Color[0] = cv.Color[1] = cv.Color[2] = 0.0f; cv.Color[3] = 0.0f;

        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv,
            IID_PPV_ARGS(&m_velocity)));

        D3D12_DESCRIPTOR_HEAP_DESC dV = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        ThrowIfFailed(device->CreateDescriptorHeap(&dV, IID_PPV_ARGS(&m_velocityRTVHeap)));

        m_velocityRTV = m_velocityRTVHeap->GetCPUDescriptorHandleForHeapStart();

        D3D12_RENDER_TARGET_VIEW_DESC rtvV{};
        rtvV.Format = DXGI_FORMAT_R16G16_FLOAT;
        rtvV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(m_velocity.Get(), &rtvV, m_velocityRTV);

        m_velocitySrvIndex = m_framework->AllocateSrvDescriptor();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        auto srvCPU0 = m_framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart();
        auto srvGPU0 = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        UINT srvInc = m_framework->GetSrvDescriptorSize();
        device->CreateShaderResourceView(m_velocity.Get(), &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCPU0, m_velocitySrvIndex, srvInc));
        m_velocitySRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGPU0, m_velocitySrvIndex, srvInc);

        m_velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    {
        auto* device = m_framework->GetDevice();
        auto* rtvHeap = m_framework->GetRtvHeap();
        const UINT rtvInc = m_framework->GetRtvDescriptorSize();
        const auto rtvHeapDesc = rtvHeap->GetDesc();
        const UINT last = rtvHeapDesc.NumDescriptors - 1;

        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC ldrDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            (UINT)m_framework->GetWidth(),
            (UINT)m_framework->GetHeight(),
            1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        D3D12_CLEAR_VALUE ldrClear{};
        ldrClear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ldrClear.Color[0] = ldrClear.Color[1] = ldrClear.Color[2] = 0.0f; ldrClear.Color[3] = 1.0f;

        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE, &ldrDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &ldrClear, IID_PPV_ARGS(&m_historyA)));

        ThrowIfFailed(device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE, &ldrDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &ldrClear, IID_PPV_ARGS(&m_historyB)));
        
        m_historyA->SetName(L"TAA_HistoryA");
        m_historyB->SetName(L"TAA_HistoryB");

        m_historyAState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_historyBState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        D3D12_DESCRIPTOR_HEAP_DESC dHA = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        D3D12_DESCRIPTOR_HEAP_DESC dHB = dHA;

        ThrowIfFailed(device->CreateDescriptorHeap(&dHA, IID_PPV_ARGS(&m_historyARTVHeap)));
        ThrowIfFailed(device->CreateDescriptorHeap(&dHB, IID_PPV_ARGS(&m_historyBRTVHeap)));

        m_historyARTV = m_historyARTVHeap->GetCPUDescriptorHandleForHeapStart();
        m_historyBRTV = m_historyBRTVHeap->GetCPUDescriptorHandleForHeapStart();

        device->CreateRenderTargetView(m_historyA.Get(), nullptr, m_historyARTV);
        device->CreateRenderTargetView(m_historyB.Get(), nullptr, m_historyBRTV);

        m_historyASrvIndex = m_framework->AllocateSrvDescriptor();
        m_historyBSrvIndex = m_framework->AllocateSrvDescriptor();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        auto  srvCPU0 = m_framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart();
        auto  srvGPU0 = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        UINT  srvInc = m_framework->GetSrvDescriptorSize();

        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHA(srvCPU0, m_historyASrvIndex, srvInc);
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHB(srvCPU0, m_historyBSrvIndex, srvInc);
        device->CreateShaderResourceView(m_historyA.Get(), &srvDesc, cpuHA);
        device->CreateShaderResourceView(m_historyB.Get(), &srvDesc, cpuHB);

        m_historyASRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGPU0, m_historyASrvIndex, srvInc);
        m_historyBSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGPU0, m_historyBSrvIndex, srvInc);
    }

    {
        const DXGI_FORMAT kDepthHistoryFormat = DXGI_FORMAT_R32_FLOAT;
        CD3DX12_RESOURCE_DESC prevDepthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            kDepthHistoryFormat, m_framework->GetWidth(), m_framework->GetHeight(), 1, 1, 1, 0, D3D12_RESOURCE_FLAG_NONE);

        CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProperties, D3D12_HEAP_FLAG_NONE,
            &prevDepthDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&m_prevDepth)));

        m_prevDepthSrvIndex = m_framework->AllocateSrvDescriptor();
        D3D12_SHADER_RESOURCE_VIEW_DESC zSrv{};
        zSrv.Format = kDepthHistoryFormat;
        zSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        zSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        zSrv.Texture2D.MipLevels = 1;
        auto srvCPU0 = m_framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart();
        auto srvGPU0 = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        UINT srvInc = m_framework->GetSrvDescriptorSize();
        device->CreateShaderResourceView(m_prevDepth.Get(), &zSrv,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCPU0, m_prevDepthSrvIndex, srvInc));
        m_prevDepthSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGPU0, m_prevDepthSrvIndex, srvInc);
    }

    m_postA->SetName(L"PostA");
    m_postB->SetName(L"PostB");
    m_velocity->SetName(L"VelocityRT");
    m_lightAccum->SetName(L"LightAccumHDR");

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

    {
        m_prevViewProj = view * proj;
        m_resetHistory = true;
        m_taaFrameIndex = 0;
    }
}

void RenderingSystem::Update(float)
{
    m_frameIndex++;
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

    if (m_autoSun && !lights.empty())
    {
        if (m_dayLengthSec < 0.001f) m_dayLengthSec = 0.001f;
        m_timeOfDaySec += dt;
        while (m_timeOfDaySec >= m_dayLengthSec) m_timeOfDaySec -= m_dayLengthSec;
        while (m_timeOfDaySec < 0.0f) m_timeOfDaySec += m_dayLengthSec;

        const float phase = m_timeOfDaySec / m_dayLengthSec;

        const float angle = phase * XM_2PI + XM_PIDIV2;

        const float elev = sinf(angle);
        const float horiz = cosf(angle);

        const float az = XMConvertToRadians(m_sunAzimuthDeg);

        XMFLOAT3 sunPos =
        {
            horiz * cosf(az),
            elev,
            horiz * sinf(az)
        };

        XMFLOAT3 sunDir =
        {
            -sunPos.x,
            -sunPos.y,
            -sunPos.z
        };

        XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&sunDir));
        XMStoreFloat3(&sunDir, v);

        lights[0].direction = sunDir;
        direction = sunDir;
        lights[0].type = 0;
    }
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

    UpdateRaytracingTLAS();

    UpdateTerrainBrush(cmd, dt);

    //TerrainPass();

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
        ImGui::Text("Frame: %d", m_frameIndex);

        ImGui::Text("draw: %d | mesh: %d", drawIndexedCount, meshDispatchCount);

        ImGui::Checkbox("Draw", &tmp);

        ImGui::End();
    }

    {
        ImGui::Begin("Object");
        
        ImGui::InputInt("Object idx", &objectIdx, 1);

        ImGui::SliderFloat("Rot x deg", &m_objectRotationDeg.x, 0.0f, 360.0f);
        ImGui::SliderFloat("Rot y deg", &m_objectRotationDeg.y, 0.0f, 360.0f);
        ImGui::SliderFloat("Rot z deg", &m_objectRotationDeg.z, 0.0f, 360.0f);

        if (!m_objects.empty()) 
        {
            m_objects[objectIdx].rotation = XMFLOAT3
            (
                XMConvertToRadians(m_objectRotationDeg.x),
                XMConvertToRadians(m_objectRotationDeg.y),
                XMConvertToRadians(m_objectRotationDeg.z)
            );

            ImGui::InputFloat("Scale", &m_objectScale, 5.0f);
            m_objects[objectIdx].scale = { m_objectScale, m_objectScale, m_objectScale };

            ImGui::SliderFloat("Pos x", &m_objects[objectIdx].position.x, -2.0f, 2.0f);
            ImGui::SliderFloat("Pos y", &m_objects[objectIdx].position.y, -2.0f, 2.0f);
            ImGui::SliderFloat("Pos z", &m_objects[objectIdx].position.z, -2.0f, 2.0f);
        }

        ImGui::InputFloat("Use normal map", &m_useNormalMap, 1.0f);
        ImGui::Checkbox("Use Roughness Map", &m_useRoughMapUI);
        ImGui::Checkbox("Use Metallic Map", &m_useMetalMapUI);
        ImGui::Checkbox("Use AO Map", &m_useAOMapUI);

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

        ImGui::Checkbox("Auto day/night", &m_autoSun);

        if (m_autoSun)
        {
            ImGui::SliderFloat("Day length (sec)", &m_dayLengthSec, 5.0f, 300.0f);
            ImGui::SliderFloat("Sun azimuth (deg)", &m_sunAzimuthDeg, 0.0f, 360.0f);

            float phase = (m_dayLengthSec > 0.001f) ? (m_timeOfDaySec / m_dayLengthSec) : 0.0f;
            if (ImGui::SliderFloat("Time of day", &phase, 0.0f, 1.0f))
            {
                m_timeOfDaySec = phase * m_dayLengthSec;
            }
                
            ImGui::Text("SunDir: %.3f %.3f %.3f", direction.x, direction.y, direction.z);
        }
        else
        {
            ImGui::SliderFloat("Light x", &direction.x, -50.0f, 50.0f);
            ImGui::SliderFloat("Light y", &direction.y, -50.0f, 50.0f);
            ImGui::SliderFloat("Light z", &direction.z, -50.0f, 50.0f);
            lights[0].direction = direction;
        }

        ImGui::InputFloat("Switch shadows mode", &ShadowsMode);

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

    {
        ImGui::Begin("TAA");

        ImGui::Checkbox("TAA", &m_enableTAA);
    
        ImGui::End();
    }

    {
        ImGui::Begin("Atmosphere");

        ImGui::Checkbox("Enable fog", &m_fogEnabled);
        ImGui::ColorEdit3("Fog color", &m_fogColor.x);

        ImGui::SliderFloat("Global density", &m_fogDensity, 0.0f, 0.01f, "%.5f");
        ImGui::SliderFloat("Height falloff", &m_fogHeightFalloff, 0.0f, 0.5f);
        ImGui::SliderFloat("Base height", &m_fogBaseHeight, -500.0f, 500.0f);
        ImGui::SliderFloat("Max opacity", &m_fogMaxOpacity, 0.0f, 1.0f);

        ImGui::End();
    }

    {
        ImGui::Begin("RT alpha");
        int prev = m_grassObjectIndex;
        ImGui::InputInt("Grass obj index", &m_grassObjectIndex);

        ImGui::Checkbox("UV from XZ", (bool*)&m_grassUsesXZ);
        ImGui::InputFloat2("UV scale", (float*)&m_grassUvScale);
        ImGui::InputFloat2("UV offset", (float*)&m_grassUvOffset);

        if (m_grassObjectIndex != prev)
        {
            UpdateGrassSrvHandle();
        }
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

    const float w = static_cast<float>(m_framework->GetWidth());
    const float h = static_cast<float>(m_framework->GetHeight());
    const float aspect = w / h;

    XMMATRIX P = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, m_near, m_far);

    if (m_enableTAA)
    {
        m_taaJitterPixPrev = m_taaJitterPix;
        m_taaJitterClipPrev = m_taaJitterClip;

        const uint32_t idx = (m_taaFrameIndex & 1023u) + 1u;
        const float jxPix = Halton(idx, 2) - 0.5f;
        const float jyPix = Halton(idx, 3) - 0.5f;
        m_taaJitterPix = { jxPix, jyPix };

        const float sx = (2.0f * jxPix) / w;
        const float sy = -(2.0f * jyPix) / h;
        m_taaJitterClip = { sx, sy };

        XMFLOAT4X4 Pf; XMStoreFloat4x4(&Pf, P);
        Pf._31 += sx;
        Pf._32 += sy;
        P = XMLoadFloat4x4(&Pf);
    }
    else
    {
        m_taaJitterPix = m_taaJitterPixPrev = { 0,0 };
        m_taaJitterClip = m_taaJitterClipPrev = { 0,0 };
    }

    proj = P;
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

        mcb.useRoughMap = m_useRoughMapUI ? 1u : 0u;
        mcb.useMetalMap = m_useMetalMapUI ? 1u : 0u;
        mcb.useAOMap = m_useAOMapUI ? 1u : 0u;

        mcb.baseColor = { obj->material.diffuse.x,
                          obj->material.diffuse.y,
                          obj->material.diffuse.z,
                          1.0f };

        memcpy(m_pCbData + static_cast<UINT>(i) * cbSize, &cb, sizeof(cb));
        memcpy(m_pMaterialData + static_cast<UINT>(i) * materialSize, &mcb, sizeof(mcb));
    }
}

inline float saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
inline XMFLOAT2 saturate(const XMFLOAT2& v) { return { saturate(v.x), saturate(v.y) }; }
inline XMFLOAT3 saturate(const XMFLOAT3& v) { return { saturate(v.x), saturate(v.y), saturate(v.z) }; }
inline XMFLOAT4 saturate(const XMFLOAT4& v) { return { saturate(v.x), saturate(v.y), saturate(v.z), saturate(v.w) }; }

void RenderingSystem::UpdateLightCB()
{
    const UINT lightCBSize = Align256(sizeof(LightCB));
    XMMATRIX invVP = XMMatrixInverse(nullptr, viewProj);

    XMFLOAT3 sunDir = { 0.0f, 1.0f, 0.0f }; 
    for (const Light& L : lights)
    {
        if (L.type == 0)
        {
            sunDir = XMFLOAT3(-L.direction.x, -L.direction.y, -L.direction.z);
            break;
        }
    }

    XMVECTOR sunDirV = XMVector3Normalize(XMLoadFloat3(&sunDir));
    float sunHeight = XMVectorGetY(sunDirV);

    auto clamp01 = [](float v) -> float
        {
            if (v < 0.0f) return 0.0f;
            if (v > 1.0f) return 1.0f;
            return v;
        };

    float sunIntensity = saturate(0.3f + 0.7f * sunHeight);

    sunIntensity = clamp01(sunIntensity);

    float dayFactor = sunIntensity;
    float nightFactor = 1.0f - dayFactor;

    float absSunH = (sunHeight < 0.0f) ? -sunHeight : sunHeight;
    float sunsetFactor = clamp01(1.0f - absSunH * 5.0f);

    for (size_t i = 0; i < lights.size(); ++i) 
    {
        Light& L = lights[i];
        LightCB cb{};

        cb.FrameIndex = m_frameIndex;

        XMStoreFloat4x4(&cb.InvViewProj, invVP);
        cb.Type = L.type;
        cb.LightDir = { L.direction.x, L.direction.y, L.direction.z, 0 };

        XMFLOAT3 finalColor = L.color;

        if (L.type == 0)
        {
            auto lerp3 = [](const XMFLOAT3& a, const XMFLOAT3& b, float t) -> XMFLOAT3
                {
                    return XMFLOAT3(
                        a.x + (b.x - a.x) * t,
                        a.y + (b.y - a.y) * t,
                        a.z + (b.z - a.z) * t
                    );
                };

            XMFLOAT3 dayCol{ 1.0f, 1.0f, 1.0f };
            XMFLOAT3 nightCol{ 0.001f, 0.001f, 0.001f };
            XMFLOAT3 sunsetCol{ 1.0f, 0.1f, 0.1f };

            XMFLOAT3 dnCol = lerp3(nightCol, dayCol, dayFactor);
            XMFLOAT3 warmCol = lerp3(dnCol, sunsetCol, sunsetFactor);

            float intensity = sunIntensity;

            finalColor.x = warmCol.x * intensity;
            finalColor.y = warmCol.y * intensity;
            finalColor.z = warmCol.z * intensity;
        }

        cb.LightColor = { finalColor.x, finalColor.y, finalColor.z, 0 };

        cb.LightPosRange = { L.position.x, L.position.y, L.position.z, L.radius };
        cb.SpotDirInnerCos = { L.spotDirection.x, L.spotDirection.y, L.spotDirection.z, L.innerCone() };
        cb.SpotOuterPad = { L.outerCone(), 0, 0, 0 };
        cb.ScreenSize = { float(m_framework->GetWidth()), float(m_framework->GetHeight()), 0, 0 };

        for (UINT ci = 0; ci < CSM_CASCADES; ++ci)
            cb.LightViewProj[ci] = m_lightViewProjCSM[ci];

        cb.CascadeSplits = 
        {
            m_cascadeSplits[0],
            m_cascadeSplits[1],
            m_cascadeSplits[2],
            (CSM_CASCADES > 3 ? m_cascadeSplits[3] : m_cascadeSplits[2])
        };

        XMStoreFloat4x4(&cb.View, view);
        cb.ShadowParams = { 1.0f / m_shadow->Size(), 0.001f, (float)m_shadow->CascadeCount(), ShadowsMode };

        cb.ShadowMaskParams = { m_shadowMaskTiling.x, m_shadowMaskTiling.y, m_shadowMaskStrength, 0.02f };

        cb.CameraPos = { cameraPos.x, cameraPos.y, cameraPos.z, 0.0f };

        memcpy(m_pLightData + UINT(i) * lightCBSize, &cb, sizeof(cb));
    }

    AmbientCB acb{};

    const float ambientBase = 0.03f;

    float ambientIntensity = ambientBase * (0.02f + 1.0f * sunIntensity);

    ambientIntensity = saturate(ambientIntensity);

    acb.AmbientColor = XMFLOAT4(ambientIntensity, ambientIntensity, ambientIntensity, 1.0f);

    acb.FogColorDensity = XMFLOAT4
    (
        m_fogColor.x,
        m_fogColor.y,
        m_fogColor.z,
        m_fogDensity
    );

    acb.FogParams = XMFLOAT4
    (
        m_fogHeightFalloff,
        m_fogBaseHeight,
        m_fogMaxOpacity,
        m_fogEnabled ? 1.0f : 0.0f
    );

    acb.SunParams = XMFLOAT4(dayFactor, sunsetFactor, nightFactor, 0.0f);

    memcpy(m_pAmbientData, &acb, sizeof(AmbientCB));
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

    ComPtr<ID3D12GraphicsCommandList6> cmd6;
    if (m_framework->IsMeshShaderSupported())
    {
        cmd->QueryInterface(IID_PPV_ARGS(&cmd6));
    }

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

        const size_t objIndex = (size_t)(obj - m_objects.data());
        const bool hasMeshlets =
            m_framework->IsMeshShaderSupported() &&
            cmd6 &&
            objIndex < m_meshletData.size() &&
            (size_t)lod < m_meshletData[objIndex].size() &&
            m_meshletData[objIndex][lod].meshletCount != 0;

        bool useMeshPipeline = hasMeshlets && !useTess && (obj->Color.w == 1.0f);

        auto srvStart = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        auto sampStart = m_framework->GetSamplerHeap()->GetGPUDescriptorHandleForHeapStart();
        const UINT cbSize = Align256(sizeof(CB));
        const UINT materialSize = Align256(sizeof(MaterialCB));

        useMeshPipeline = tmp;

        if (useMeshPipeline)
        {
            cmd->SetGraphicsRootSignature(m_pipeline.GetMeshletRS());
            cmd->SetPipelineState(m_pipeline.GetMeshletGBufferPSO());

            cmd->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + (UINT)i * cbSize);
            cmd->SetGraphicsRootConstantBufferView(1, m_lightBuffer->GetGPUVirtualAddress());
            cmd->SetGraphicsRootConstantBufferView(2, m_tessBuffer->GetGPUVirtualAddress());
            cmd->SetGraphicsRootDescriptorTable(3, srvStart);
            cmd->SetGraphicsRootDescriptorTable(4, sampStart);
            cmd->SetGraphicsRootConstantBufferView(5, m_materialBuffer->GetGPUVirtualAddress() + (UINT)i * materialSize);

            const UINT srvStep = m_framework->GetSrvDescriptorSize();
            const auto& md = m_meshletData[objIndex][lod];
            CD3DX12_GPU_DESCRIPTOR_HANDLE meshletTable(srvStart, (INT)md.srvBase, srvStep);
            cmd->SetGraphicsRootDescriptorTable(7, meshletTable);

            cmd6->DispatchMesh(md.meshletCount, 1, 1);
            meshDispatchCount++;
        }
        else
        {
            cmd->SetGraphicsRootSignature(m_pipeline.GetRootSignature());

            if (!switchedToTransparent && obj->Color.w != 1.0f)
            {
                cmd->SetPipelineState(m_pipeline.GetTransparentPSO());
                switchedToTransparent = true;
            }

            if (useTess)
            {
                cmd->SetPipelineState(m_wireframe ? m_pipeline.GetGBufferTessellationWireframePSO()
                    : m_pipeline.GetGBufferTessellationPSO());
                cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
            }
            else
            {
                cmd->SetPipelineState(m_pipeline.GetGBufferPSO());
                cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }

            cmd->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + (UINT)i * cbSize);
            cmd->SetGraphicsRootConstantBufferView(1, m_lightBuffer->GetGPUVirtualAddress());
            cmd->SetGraphicsRootConstantBufferView(2, m_tessBuffer->GetGPUVirtualAddress());
            cmd->SetGraphicsRootDescriptorTable(3, srvStart);
            cmd->SetGraphicsRootDescriptorTable(4, sampStart);
            cmd->SetGraphicsRootConstantBufferView(5, m_materialBuffer->GetGPUVirtualAddress() + (UINT)i * materialSize);

            cmd->IASetVertexBuffers(0, 1, &obj->lodVBs[lod]);
            cmd->IASetIndexBuffer(&obj->lodIBs[lod]);

            cmd->DrawIndexedInstanced((UINT)obj->lodMeshes[lod].indices.size(), 1, 0, 0, 0);
            drawIndexedCount++;
        }
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
    cmd->SetGraphicsRootDescriptorTable(7, m_tlasSrvGpu);
    UpdateAlphaShadowCB();
    cmd->SetGraphicsRootConstantBufferView(8, m_alphaShadowCB->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(9, m_grassSrvGpu);

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
    auto takeDstRTV = [&](bool a)->D3D12_CPU_DESCRIPTOR_HANDLE { return a ? m_postARTV : m_postBRTV; };

    auto doInter = [&](ID3D12PipelineState* pso)
        {
            ID3D12Resource* dstRes = takeDstRes(useA);
            auto dstRTV = takeDstRTV(useA);
            D3D12_GPU_DESCRIPTOR_HANDLE outSrv{};
            ApplyPassToIntermediate(pso, cur, dstRes, dstRTV, outSrv);
            cur = outSrv;
            useA = !useA;
        };

    doInter(m_enableTonemap ? m_pipeline.GetTonemapPSO()
        : m_pipeline.GetCopyHDRtoLDRPSO());

    {
        TransitionResource(cmd, m_velocity.Get(), m_velocityState, D3D12_RESOURCE_STATE_RENDER_TARGET);

        const float clearV[4] = { 0,0,0,0 };
        cmd->OMSetRenderTargets(1, &m_velocityRTV, FALSE, nullptr);
        cmd->ClearRenderTargetView(m_velocityRTV, clearV, 0, nullptr);
        m_framework->SetViewportAndScissors();

        cmd->SetGraphicsRootSignature(m_pipeline.GetPostRS());
        SetCommonHeaps();

        const auto& gbufSrvs = m_gbuffer->GetSRVs();
        D3D12_GPU_DESCRIPTOR_HANDLE currDepthSrv = gbufSrvs[3];
        cmd->SetGraphicsRootDescriptorTable(0, currDepthSrv);

        VelCBData vcb{};
        vcb.invRes = { 1.0f / m_framework->GetWidth(), 1.0f / m_framework->GetHeight() };
        vcb.jitterCur = m_taaJitterPix;
        vcb.jitterPrev = m_taaJitterPixPrev;
        XMStoreFloat4x4(&vcb.CurrInv, XMMatrixInverse(nullptr, viewProj));
        XMStoreFloat4x4(&vcb.PrevVP, m_prevViewProj);
        vcb.uvGuard = 2.0f;
        vcb.zDiffNdc = 0.004f;

        ComPtr<ID3D12Resource> velCB;
        {
            CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(VelCBData)));
            ThrowIfFailed(m_framework->GetDevice()->CreateCommittedResource(
                &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&velCB)));
            uint8_t* p = nullptr;
            velCB->Map(0, nullptr, reinterpret_cast<void**>(&p));
            memcpy(p, &vcb, sizeof(vcb));
            velCB->Unmap(0, nullptr);
            m_transientUploads.push_back(velCB);
        }
        cmd->SetGraphicsRootConstantBufferView(1, velCB->GetGPUVirtualAddress());

        cmd->SetPipelineState(m_pipeline.GetVelocityPSO());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);

        TransitionResource(cmd, m_velocity.Get(), m_velocityState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    if (m_enableTAA)
    {
        const bool writeToHistA = ((m_taaFrameIndex & 1u) == 0u);
        ID3D12Resource* dstRes = writeToHistA ? m_historyA.Get() : m_historyB.Get();
        D3D12_CPU_DESCRIPTOR_HANDLE dstRTV = writeToHistA ? m_historyARTV : m_historyBRTV;

        D3D12_GPU_DESCRIPTOR_HANDLE histSrv = writeToHistA ? m_historyBSRV : m_historyASRV;
        if (m_taaFrameIndex == 0 || m_resetHistory) histSrv = cur;

        UpdateTAACB();

        const auto& gbufSrvs = m_gbuffer->GetSRVs();
        D3D12_GPU_DESCRIPTOR_HANDLE prevDepthSrv = m_prevDepthSRV;
        D3D12_GPU_DESCRIPTOR_HANDLE currDepthSrv = gbufSrvs[3];
        D3D12_GPU_DESCRIPTOR_HANDLE velocitySrv = m_velocitySRV;

        D3D12_GPU_DESCRIPTOR_HANDLE taaOut{};
        ApplyTAAToIntermediate(cur, histSrv, prevDepthSrv, currDepthSrv, velocitySrv, dstRes, dstRTV, taaOut);
        cur = taaOut;

        m_prevViewProj = viewProj;

        CopyDepthToPrev();

        ++m_taaFrameIndex;
    }
    else
    {
        m_prevViewProj = viewProj;
        CopyDepthToPrev();
    }

    cmd->SetGraphicsRootSignature(m_pipeline.GetPostRS());
    SetCommonHeaps();
    UpdatePostCB();

    std::vector<ID3D12PipelineState*> passes;
    passes.push_back(m_pipeline.GetGammaPSO());

    if (m_enableInvert) passes.push_back(m_pipeline.GetInvertPSO());
    if (m_enableGrayscale) passes.push_back(m_pipeline.GetGrayscalePSO());
    if (m_enablePixelate) passes.push_back(m_pipeline.GetPixelatePSO());
    if (m_enablePosterize) passes.push_back(m_pipeline.GetPosterizePSO());
    if (m_enableSaturation)passes.push_back(m_pipeline.GetSaturationPSO());

    passes.push_back(m_enableVignette ? m_pipeline.GetVignettePSO()
        : m_pipeline.GetCopyLDRPSO());

    for (size_t i = 0; i < passes.size(); ++i)
    {
        bool last = (i + 1 == passes.size());
        if (last) ApplyPassToBackbuffer(passes[i], cur);
        else      doInter(passes[i]);
    }
}

void RenderingSystem::ApplyPassToIntermediate(ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE inSrv, ID3D12Resource* dst, D3D12_CPU_DESCRIPTOR_HANDLE dstRtv, D3D12_GPU_DESCRIPTOR_HANDLE& outSrv)
{
    D3D12_RESOURCE_STATES* pState =
        (dst == m_postA.Get()) ? &m_postAState : &m_postBState;

    TransitionResource(cmd, dst, *pState, D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
    const float clear[4] = { 0,0,0,1 };
    cmd->ClearRenderTargetView(dstRtv, clear, 0, nullptr);
    m_framework->SetViewportAndScissors();

    cmd->SetPipelineState(pso);
    cmd->SetGraphicsRootDescriptorTable(0, inSrv);
    cmd->SetGraphicsRootConstantBufferView(1, m_postBuffer->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    TransitionResource(cmd, dst, *pState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    outSrv = (dst == m_postA.Get()) ? m_postASRV : m_postBSRV;
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

void RenderingSystem::ApplyTAAToIntermediate(D3D12_GPU_DESCRIPTOR_HANDLE currSrv, D3D12_GPU_DESCRIPTOR_HANDLE historySrv, D3D12_GPU_DESCRIPTOR_HANDLE prevDepthSrv, D3D12_GPU_DESCRIPTOR_HANDLE currDepthSrv, D3D12_GPU_DESCRIPTOR_HANDLE velocitySrv,ID3D12Resource* dst, D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,  D3D12_GPU_DESCRIPTOR_HANDLE& outSrv)
{
    D3D12_RESOURCE_STATES* pState =
        (dst == m_historyA.Get()) ? &m_historyAState : &m_historyBState;

    TransitionResource(cmd, dst, *pState, D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
    const float clearC[4] = { 0,0,0,0 };
    cmd->ClearRenderTargetView(dstRtv, clearC, 0, nullptr);
    m_framework->SetViewportAndScissors();

    cmd->SetGraphicsRootSignature(m_pipeline.GetTAARS());
    SetCommonHeaps();

    cmd->SetGraphicsRootDescriptorTable(0, currSrv);
    cmd->SetGraphicsRootDescriptorTable(1, historySrv);
    cmd->SetGraphicsRootDescriptorTable(2, prevDepthSrv);
    cmd->SetGraphicsRootDescriptorTable(3, currDepthSrv);
    cmd->SetGraphicsRootDescriptorTable(4, velocitySrv);
    cmd->SetGraphicsRootConstantBufferView(5, m_taaCB->GetGPUVirtualAddress());

    cmd->SetPipelineState(m_pipeline.GetTAAPSO());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    TransitionResource(cmd, dst, *pState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    outSrv = (dst == m_historyA.Get()) ? m_historyASRV : m_historyBSRV;
}   

void RenderingSystem::DoTAAPass(D3D12_GPU_DESCRIPTOR_HANDLE currLDR, D3D12_GPU_DESCRIPTOR_HANDLE& outSrv, bool writeToHistoryA)
{
    ID3D12Resource* dst = writeToHistoryA ? m_historyA.Get() : m_historyB.Get();
    auto dstRTV = writeToHistoryA ? m_historyARTV : m_historyBRTV;
    auto histSrv = writeToHistoryA ? m_historyBSRV : m_historyASRV;

    if (m_taaFrameIndex == 0)
        histSrv = currLDR;

    UpdateTAACB();

    D3D12_GPU_DESCRIPTOR_HANDLE prevDepth = m_prevDepthSRV;
    D3D12_GPU_DESCRIPTOR_HANDLE currDepth = m_gbuffer->GetSRVs()[3];
    D3D12_GPU_DESCRIPTOR_HANDLE velSrv = m_velocitySRV;

    D3D12_GPU_DESCRIPTOR_HANDLE taaOut{};
    ApplyTAAToIntermediate(currLDR, histSrv, prevDepth, currDepth, velSrv, dst, dstRTV, taaOut);
    outSrv = taaOut;

    m_prevViewProj = viewProj;
    m_taaFrameIndex++;
}

void RenderingSystem::UpdateTAACB()
{
    TAACB cb{};

    cb.jitterCur = m_taaJitterPix;
    cb.jitterPrev = m_taaJitterPixPrev;

    cb.alpha = std::clamp(m_taaAlpha, 0.0f, 1.0f);
    cb.invResolution = { 1.0f / m_framework->GetWidth(), 1.0f / m_framework->GetHeight() };
    cb.clampK = 0.01f;
    cb.reactiveK = 0.28f;

    XMStoreFloat4x4(&cb.CurrViewProjInv, XMMatrixInverse(nullptr, viewProj));
    XMStoreFloat4x4(&cb.PrevViewProj, m_prevViewProj);

    cb.zDiffNdc = 0.0045f;
    cb.uvGuard = 2.0f;

    if (m_resetHistory)
    {
        cb.alpha = 1.0f;
        m_resetHistory = false;
    }

    memcpy(m_pTaaData, &cb, sizeof(cb));
}

void RenderingSystem::CopyDepthToPrev()
{
    ID3D12Resource* src = m_gbuffer->GetDepthResource();
    ID3D12Resource* dst = m_prevDepth.Get();

    {
        auto srcToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
            src,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_COPY_SOURCE
        );
        auto dstToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
            dst,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_DEST
        );
        cmd->ResourceBarrier(1, &srcToCopy);
        cmd->ResourceBarrier(1, &dstToCopy);
    }

    cmd->CopyResource(dst, src);

    {
        auto srcBack = CD3DX12_RESOURCE_BARRIER::Transition(
            src,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        auto dstBack = CD3DX12_RESOURCE_BARRIER::Transition(
            dst,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        cmd->ResourceBarrier(1, &srcBack);
        cmd->ResourceBarrier(1, &dstBack);
    }
}

void RenderingSystem::TransitionResource(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res, D3D12_RESOURCE_STATES& current, D3D12_RESOURCE_STATES target)
{
    if (current == target) return;
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(res, current, target);
    cmd->ResourceBarrier(1, &barrier);
    current = target;
}

static ComPtr<ID3D12Resource> CreateUavBuffer(ID3D12Device* device, UINT64 size, D3D12_RESOURCE_STATES initState)
{
    ComPtr<ID3D12Resource> res;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, initState, nullptr, IID_PPV_ARGS(&res)));

    return res;
}

static ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 size)
{
    ComPtr<ID3D12Resource> res;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&res)));

    return res;
}

void RenderingSystem::BuildRaytracingAS()
{
    ID3D12Device* device = m_framework->GetDevice();
    ID3D12GraphicsCommandList* cmd = m_framework->GetCommandList();

    ComPtr<ID3D12Device5> device5;
    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device5)));

    ComPtr<ID3D12GraphicsCommandList4> cmd4;
    ThrowIfFailed(cmd->QueryInterface(IID_PPV_ARGS(&cmd4)));

    BuildBLAS_Once(device5.Get(), cmd4.Get());
    BuildOrUpdateTLAS(device5.Get(), cmd4.Get(), false);

    m_rtBuilt = true;
}

void RenderingSystem::UpdateRaytracingTLAS()
{
    if (!m_rtBuilt) return;

    ID3D12Device* device = m_framework->GetDevice();
    ID3D12GraphicsCommandList* cmd = m_framework->GetCommandList();

    ComPtr<ID3D12Device5> device5;
    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device5)));

    ComPtr<ID3D12GraphicsCommandList4> cmd4;
    ThrowIfFailed(cmd->QueryInterface(IID_PPV_ARGS(&cmd4)));

    BuildOrUpdateTLAS(device5.Get(), cmd4.Get(), true);
}

void RenderingSystem::BuildBLAS_Once(ID3D12Device5* device5, ID3D12GraphicsCommandList4* cmd4)
{
    if (m_blasBuilt) return;

    ID3D12Device* device = m_framework->GetDevice();
    ID3D12GraphicsCommandList* cmd = m_framework->GetCommandList();

    m_blas.clear();
    m_blas.resize(m_objects.size());

    m_blasScratch.clear();
    m_blasScratch.resize(m_objects.size());

    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        auto& obj = m_objects[i];
        if (obj.lodMeshes.empty()) continue;

        const auto& vbv = obj.lodVBs[0];
        const auto& ibv = obj.lodIBs[0];

        D3D12_RAYTRACING_GEOMETRY_DESC geom{};
        geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        //geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

        geom.Triangles.VertexBuffer.StartAddress = vbv.BufferLocation;
        geom.Triangles.VertexBuffer.StrideInBytes = vbv.StrideInBytes;
        geom.Triangles.VertexCount = vbv.SizeInBytes / vbv.StrideInBytes;
        geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

        geom.Triangles.IndexBuffer = ibv.BufferLocation;
        geom.Triangles.IndexCount = ibv.SizeInBytes / sizeof(uint32_t);
        geom.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs = 1;
        inputs.pGeometryDescs = &geom;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

        m_blasScratch[i] = CreateUavBuffer(device, info.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            ThrowIfFailed(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                nullptr, IID_PPV_ARGS(&m_blas[i])));
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        build.Inputs = inputs;
        build.ScratchAccelerationStructureData = m_blasScratch[i]->GetGPUVirtualAddress();
        build.DestAccelerationStructureData = m_blas[i]->GetGPUVirtualAddress();

        cmd4->BuildRaytracingAccelerationStructure(&build, 0, nullptr);

        auto uav = CD3DX12_RESOURCE_BARRIER::UAV(m_blas[i].Get());
        cmd->ResourceBarrier(1, &uav);
    }

    m_blasBuilt = true;
}

void RenderingSystem::BuildOrUpdateTLAS(ID3D12Device5* device5, ID3D12GraphicsCommandList4* cmd4, bool update)
{
    ID3D12Device* device = m_framework->GetDevice();
    ID3D12GraphicsCommandList* cmd = m_framework->GetCommandList();

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
    instances.reserve(m_objects.size());

    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        if (i >= m_blas.size() || !m_blas[i]) continue;

        D3D12_RAYTRACING_INSTANCE_DESC inst{};
        inst.InstanceID = (UINT)i;
        inst.InstanceContributionToHitGroupIndex = 0;
        inst.InstanceMask = 0xFF;
        //inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE | D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        inst.AccelerationStructure = m_blas[i]->GetGPUVirtualAddress();

        XMMATRIX W = m_objects[i].GetWorldMatrix();
        XMFLOAT4X4 wf;
        XMStoreFloat4x4(&wf, W);

        inst.Transform[0][0] = wf._11; inst.Transform[0][1] = wf._12; inst.Transform[0][2] = wf._13; inst.Transform[0][3] = wf._14;
        inst.Transform[1][0] = wf._21; inst.Transform[1][1] = wf._22; inst.Transform[1][2] = wf._23; inst.Transform[1][3] = wf._24;
        inst.Transform[2][0] = wf._31; inst.Transform[2][1] = wf._32; inst.Transform[2][2] = wf._33; inst.Transform[2][3] = wf._34;


        instances.push_back(inst);
    }

    if (update && (UINT)instances.size() != m_tlasInstanceCount) update = false;

    const UINT64 instBytes = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instances.size();
    if (!m_tlasInstanceUpload || instBytes != m_tlasInstanceBytes)
    {
        m_tlasInstanceUpload.Reset();
        m_tlasInstanceUpload = CreateUploadBuffer(device, instBytes);
        m_tlasInstanceBytes = instBytes;
        m_tlasInstanceCount = (UINT)instances.size();
        update = false;
    }

    void* mapped = nullptr;
    ThrowIfFailed(m_tlasInstanceUpload->Map(0, nullptr, &mapped));
    memcpy(mapped, instances.data(), (size_t)instBytes);
    m_tlasInstanceUpload->Unmap(0, nullptr);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = (UINT)instances.size();
    inputs.InstanceDescs = m_tlasInstanceUpload->GetGPUVirtualAddress();
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

    if (update) inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

    if (!m_tlas || !update)
    {
        device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &m_tlasPrebuild);

        UINT64 scratchSize = m_tlasPrebuild.ScratchDataSizeInBytes;
        scratchSize = max(scratchSize, m_tlasPrebuild.UpdateScratchDataSizeInBytes);

        m_tlasScratch.Reset();
        m_tlasScratch = CreateUavBuffer(device, scratchSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(
                m_tlasPrebuild.ResultDataMaxSizeInBytes,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            m_tlas.Reset();
            ThrowIfFailed(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                nullptr, IID_PPV_ARGS(&m_tlas)));
        }

        m_tlasSrvIndex = m_framework->AllocateSrvDescriptor();
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += SIZE_T(m_tlasSrvIndex) * m_framework->GetSrvDescriptorSize();

        m_tlasSrvGpu = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
        m_tlasSrvGpu.ptr += UINT64(m_tlasSrvIndex) * m_framework->GetSrvDescriptorSize();

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress();
        device->CreateShaderResourceView(nullptr, &srv, cpu);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    build.ScratchAccelerationStructureData = m_tlasScratch->GetGPUVirtualAddress();
    build.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    build.SourceAccelerationStructureData = update ? m_tlas->GetGPUVirtualAddress() : 0;

    cmd4->BuildRaytracingAccelerationStructure(&build, 0, nullptr);

    auto uav = CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.Get());
    cmd->ResourceBarrier(1, &uav);
}

void RenderingSystem::InitAlphaShadowDemoResources()
{
    auto* device = m_framework->GetDevice();

    if (!m_alphaShadowCB)
    {
        const UINT cbSize = Align256(sizeof(AlphaShadowCBData));
        m_alphaShadowCB = CreateUploadBuffer(device, cbSize);
    }

    UpdateGrassSrvHandle();

    UpdateAlphaShadowCB();
}

void RenderingSystem::UpdateGrassSrvHandle()
{
    if (m_objects.empty()) return;

    if (m_grassObjectIndex < 0) m_grassObjectIndex = 0;
    if (m_grassObjectIndex >= (int)m_objects.size()) m_grassObjectIndex = (int)m_objects.size() - 1;

    m_grassSrvIndex = m_objects[(size_t)m_grassObjectIndex].texIdx[0];

    auto gpuStart = m_framework->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
    gpuStart.ptr += UINT64(m_grassSrvIndex) * m_framework->GetSrvDescriptorSize();
    m_grassSrvGpu = gpuStart;
}

void RenderingSystem::UpdateAlphaShadowCB()
{
    if (!m_alphaShadowCB) return;

    AlphaShadowCBData data{};
    data.GrassInstanceID = (uint32_t)m_grassObjectIndex;
    data.GrassUsesXZ = m_grassUsesXZ;
    data.GrassAlphaCutoff = m_grassAlphaCutoff;
    data.GrassUvScale = m_grassUvScale;
    data.GrassUvOffset = m_grassUvOffset;

    void* p = nullptr;
    ThrowIfFailed(m_alphaShadowCB->Map(0, nullptr, &p));
    memcpy(p, &data, sizeof(data));
    m_alphaShadowCB->Unmap(0, nullptr);
}
