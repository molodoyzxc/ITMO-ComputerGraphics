#pragma once
#include "Pipeline.h"
#include "InputDevice.h"
#include "Meshes.h"
#include <wrl/client.h>
#include <d3d12.h>
#include <vector>
#include "SceneObject.h"
#include "IGameApp.h"
#include "AssetLoader.h"
#include "GBuffer.h"
#include "Light.h"
#include "Timer.h"
#include "ShadowMap.h"
#include <array>
#include "ParticleSystem.h"
#include "Octree.h"
#include "Terrain.h"

using Microsoft::WRL::ComPtr;

class DX12Framework;

class RenderingSystem
{
public:
    RenderingSystem(DX12Framework* framework, InputDevice* input);

    void Initialize();
    void Update(float dt);
    void Render();

private:
    DX12Framework* m_framework;
    InputDevice* m_input;
    Pipeline m_pipeline;
    AssetLoader loader;
    std::unique_ptr<GBuffer> m_gbuffer;
    std::unique_ptr <ShadowMap> m_shadow;
    Timer timer;

    uint32_t m_frameIndex = 0;

    std::vector<SceneObject> m_objects;
    std::vector<SceneObject*> m_visibleObjects;
    std::vector<Light> lights;

    ComPtr<ID3D12Resource> m_constantBuffer;
    ComPtr<ID3D12Resource> m_lightBuffer;
    ComPtr<ID3D12Resource> m_ambientBuffer;
    ComPtr<ID3D12Resource> m_tessBuffer;
    ComPtr<ID3D12Resource> m_materialBuffer;
    ComPtr<ID3D12Resource> m_shadowBuffer;
    ComPtr<ID3D12Resource> m_postBuffer;

    uint8_t* m_pCbData = nullptr;
    uint8_t* m_pLightData = nullptr;
    uint8_t* m_pAmbientData = nullptr;
    uint8_t* m_pTessCbData = nullptr;
    uint8_t* m_pMaterialData = nullptr;
    uint8_t* m_pShadowCbData = nullptr;
    uint8_t* m_pPostData = nullptr;

    XMMATRIX view, proj, viewProj;
    ID3D12GraphicsCommandList* cmd;
    XMFLOAT4X4 m_lightViewProj;

    float m_yaw = 0.f;
    float m_pitch = 0.f;    
    XMFLOAT3 cameraPos{ 0.0f, 0.0f, 0.0f };
    float m_near = 0.1f;
    float m_far = 5000.0f;

    float cameraSpeed = 1.0f;
    float acceleration = 3.0f;
    float deceleration = 0.1f;
    float rotationSpeed = 0.02f;

    float m_heightScale = 0.0f;
    float m_maxTess = 1.0f;
    bool  m_wireframe = false;
    float m_objectScale = 1.0f;
    XMFLOAT3 m_objectRotationDeg{ 0,0,0 };
    int objectIdx = 0;
    float m_useNormalMap = 0.0f;
    bool m_useRoughMapUI = true;
    bool m_useMetalMapUI = true;
    bool m_useAOMapUI = true;
    float m_fakeCameraZ = 0.0f;

    float m_currentFPS = 0.0f;
    float dt;

    XMFLOAT3 direction = {1.0f, -50.0f, 0.0f};
    static constexpr UINT CSM_CASCADES = 4;
    XMFLOAT4X4 m_lightViewProjCSM[CSM_CASCADES];
    float m_cascadeSplits[CSM_CASCADES];
    std::array<std::vector<SceneObject*>, CSM_CASCADES> m_shadowCasters;
    std::array<float, CSM_CASCADES> m_biasPerCascade{};

    std::unique_ptr<ParticleSystem> m_particles;

    ComPtr<ID3D12Resource> m_lightAccum;
    D3D12_CPU_DESCRIPTOR_HANDLE m_lightAccumRTV{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_lightAccumSRV{};
    UINT m_lightAccumSrvIndex = 0;
    float postExposure = 1.0f;
    float postGamma = 2.2f;
    float postVignetteStrength = 0.20f;
    float postVignettePower = 2.0f;
    XMFLOAT2 postVignetteCenter{ 0.5f, 0.5f };
    int postTonemap = 2;

    ComPtr<ID3D12Resource> m_postA;
    ComPtr<ID3D12Resource> m_postB;
    D3D12_CPU_DESCRIPTOR_HANDLE m_postARTV{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_postBRTV{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_postASRV{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_postBSRV{};
    UINT m_postASrvIndex = 0;
    UINT m_postBSrvIndex = 0;

    ComPtr<ID3D12DescriptorHeap> m_lightAccumRTVHeap;
    ComPtr<ID3D12DescriptorHeap> m_velocityRTVHeap;
    ComPtr<ID3D12DescriptorHeap> m_historyARTVHeap, m_historyBRTVHeap;

    bool m_enableTonemap = true;
    bool m_enableGamma = true;
    bool m_enableVignette = true;
    bool m_enableInvert = false;
    bool m_enableGrayscale = false;
    bool m_enablePixelate = false;
    bool m_enablePosterize = false;
    bool m_enableSaturation = false;
    float pixelateSize = 1.0f;
    float posterizeLevels = 2.0f;
    float saturation = 1.0f;

    struct IBLSet
    {
        UINT irradianceSrv = UINT(-1);
        UINT prefilteredSrv = UINT(-1);
        UINT brdfSrv = UINT(-1);
        CD3DX12_GPU_DESCRIPTOR_HANDLE tableStart{};
    };
    IBLSet m_ibl;

    std::unique_ptr<Octree> m_octree;

    UINT m_shadowMaskSrvIndex = UINT(-1);
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_shadowMaskSRV{};
    XMFLOAT2 m_shadowMaskTiling{ 0.1f, 0.1f };
    float m_shadowMaskStrength = 0.0f;

    bool m_previewGBuffer = false;
    struct PreviewCB 
    {
        int mode;  // 0: Base Color, 1: Normal, 2: Depth, 3: Roughness, 4: Metallic, 5: AO
        float nearPlane;
        float farPlane;
        float pad;    
    };
    ComPtr<ID3D12Resource> m_previewBuffer;
    uint8_t * m_pPreviewData = nullptr;
    UINT m_previewCBStride = 0;

    D3D12_RESOURCE_STATES m_backBufferState = D3D12_RESOURCE_STATE_PRESENT;

    std::unique_ptr<Terrain> m_terrain;
    UINT m_heightmapSrvIndex = UINT(-1);
    float m_terrainWorldSize = 2048.0f;
    float m_terrainHeight = 800.0f;
    int m_terrainMaxDepth = 4;
    /*
    4^0 = 1
    4^1 = 4
    4^2 = 16
    4^3 = 64
    4^4 = 256
    4^5 = 1024
    4^6 = 4096
    */
    float m_terrainSkirt = 10.0f;
    float m_screenTau = 0.0f;
    float offsetX = 0, offsetZ = 0;
        
    struct TerrainBrush
    {
        bool  enabled = false;
        bool  invert = false;     
        float radiusWorld = 20.0f;
        float strength = 0.15f;   
        float hardness = 0.5f;    
        bool  painting = false;
    } m_brush;

    ComPtr<ID3D12Resource> m_heightDeltaTex;
    UINT m_heightDeltaSrvIndex = 0;
    int m_heightDeltaW = 1024;
    int m_heightDeltaH = 1024;
    std::vector<float> m_heightDeltaCPU;
    std::vector<ComPtr<ID3D12Resource>> m_transientUploads;

    ComPtr<ID3D12Resource> m_uvRT;             
    D3D12_CPU_DESCRIPTOR_HANDLE m_uvRTV{};          
    UINT m_uvRTVIndex = UINT_MAX;
    ComPtr<ID3D12PipelineState> m_terrainUV_PSO;
    ComPtr<ID3D12RootSignature> m_sharedRS;     
    ComPtr<ID3D12Resource> m_uvReadback;
    UINT64 m_uvReadbackPitch = 0;

    ComPtr<ID3D12Resource> m_depthStaging;
    UINT64 m_depthRowPitch = 0;          
    UINT m_depthWidth = 0;               
    UINT m_depthHeight = 0;

    bool m_enableTAA = false;
    UINT m_taaFrameIndex = 0;
    XMFLOAT2 m_taaJitterPix{ 0,0 };
    XMFLOAT2 m_taaJitterPixPrev{ 0,0 };
    XMFLOAT2 m_taaJitterClip{ 0,0 };
    XMFLOAT2 m_taaJitterClipPrev{ 0,0 };
    float m_taaAlpha = 0.1f;
    bool m_resetHistory = true;

    ComPtr<ID3D12Resource> m_historyA;
    ComPtr<ID3D12Resource> m_historyB;
    D3D12_CPU_DESCRIPTOR_HANDLE m_historyARTV{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_historyBRTV{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_historyASRV{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_historyBSRV{};
    UINT m_historyASrvIndex = 0;
    UINT m_historyBSrvIndex = 0;

    ComPtr<ID3D12Resource> m_taaCB;
    uint8_t* m_pTaaData = nullptr;

    ComPtr<ID3D12Resource> m_prevDepth;
    UINT m_prevDepthSrvIndex = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE m_prevDepthSRV{};
    XMMATRIX m_prevViewProj;

    ComPtr<ID3D12Resource> m_velocity;
    D3D12_CPU_DESCRIPTOR_HANDLE m_velocityRTV;
    D3D12_GPU_DESCRIPTOR_HANDLE m_velocitySRV;
    UINT m_velocitySrvIndex;

    D3D12_RESOURCE_STATES m_historyAState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_historyBState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_postAState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_postBState = D3D12_RESOURCE_STATE_RENDER_TARGET;


    XMFLOAT3 m_fogColor{ 0.9f, 0.9f, 0.9f };
    float m_fogDensity = 0.0004f;
    float m_fogHeightFalloff = 0.05f;
    float m_fogBaseHeight = 0.0f;    
    float m_fogMaxOpacity = 1.0f;    
    bool m_fogEnabled = false;

    bool m_blasBuilt = false;

    std::vector<ComPtr<ID3D12Resource>> m_blas;
    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasScratch;
    ComPtr<ID3D12Resource> m_tlasInstanceUpload;

    UINT m_tlasSrvIndex = UINT_MAX;
    D3D12_GPU_DESCRIPTOR_HANDLE m_tlasSrvGpu{};
    bool m_rtBuilt = false;
    float ShadowsMode = 1.0f;
    std::vector<ComPtr<ID3D12Resource>> m_blasScratch;

    UINT m_tlasInstanceCount = 0;
    UINT64 m_tlasInstanceBytes = 0;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO m_tlasPrebuild = {};

    struct MeshletDrawData
    {
        ComPtr<ID3D12Resource> meshlets;
        ComPtr<ID3D12Resource> meshletVertices;
        ComPtr<ID3D12Resource> meshletPrims;
        uint32_t meshletCount = 0;
        uint32_t srvBase = 0; 
    };

    std::vector<std::vector<MeshletDrawData>> m_meshletData;
    std::vector<ComPtr<ID3D12Resource>> m_meshletUploads;

    UINT drawIndexedCount = 0;
    UINT meshDispatchCount = 0;
    bool tmp = true;

    bool  m_autoSun = false;
    float m_dayLengthSec = 60.0f;
    float m_timeOfDaySec = 0.0f;
    float m_sunAzimuthDeg = 0.0f;

    ComPtr<ID3D12Resource> m_alphaShadowCB;
    UINT m_grassSrvIndex = UINT(-1);
    D3D12_GPU_DESCRIPTOR_HANDLE m_grassSrvGpu{};

    int m_grassObjectIndex = 1;
    float m_grassAlphaCutoff = 0.1f;
    uint32_t m_grassUsesXZ = 1;
    XMFLOAT2 m_grassUvScale = { 0.1f, 0.1f };
    XMFLOAT2 m_grassUvOffset = { 0.5f, 0.5f };

    ComPtr<ID3D12Resource> m_lightingColor;
    UINT m_lightingColorRtvIndex = UINT(-1);
    UINT m_lightingColorSrvIndex = UINT(-1);
    D3D12_GPU_DESCRIPTOR_HANDLE m_lightingColorSrvGpu{};

    ComPtr<ID3D12Resource> m_motionBlurCB;
    uint8_t* m_pMotionBlurData = nullptr;
    bool m_mbEnabled = false;
    float m_mbStrength = 1.0f;
    float m_mbMaxPixels = 32.0f;
    uint32_t m_mbSamples = 12;

    XMMATRIX m_viewProj_NoJitter;
    XMMATRIX m_prevViewProj_NoJitter;
    XMMATRIX m_invViewProj_NoJitter;

private:
    static UINT Align256(UINT size) { return (size + 255) & ~255u; }
    static inline void ThrowIfFailed(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("HRESULT failed"); }

    void KeyboardControl();
    void CountFPS();

    void SetObjects();
    void SetLights();
    void LoadErrorTextures();
    void LoadTextures();
    void CreateConstantBuffers();

    void UpdateUI();
    void BuildViewProj();
    void UpdateTessellationCB();
    void ExtractVisibleObjects();
    void UpdatePerObjectCBs();
    void UpdateLightCB();
    void UpdatePostCB();
    void GeometryPass();
    void DeferredPass();
    void SetCommonHeaps();

    void ShadowPass();
    void BuildLightViewProjCSM();
    void ExtractShadowCastersForCascade
    (
        UINT ci,
        const XMMATRIX& LV,
        float minX, float maxX,
        float minY, float maxY,
        float minZ, float maxZ
    );

    void RebuildOctree();
    static void ComputeLocalSphereFromMesh(const Mesh& m, XMFLOAT3& c, float& r);
    static AABB MakeWorldAABBFromSphere(const XMMATRIX& world, const XMFLOAT3& cLocal, float rLocal, const XMFLOAT3& scale);

    void PostProcessPass();
    void ApplyPassToIntermediate(ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE inSrv, ID3D12Resource* dst, D3D12_CPU_DESCRIPTOR_HANDLE dstRtv, D3D12_GPU_DESCRIPTOR_HANDLE& outSrv);
    void ApplyPassToBackbuffer(ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE inSrv);

    void PreviewGBufferPass();

    void TerrainPass();

    void InitHeightDeltaTexture();
    void UpdateTerrainBrush(ID3D12GraphicsCommandList* cmd, float dt);
    bool ScreenToWorldRay(float mx, float my, XMVECTOR& ro, XMVECTOR& rd);
    bool RayPlaneY0(const XMVECTOR& ro, const XMVECTOR& rd, XMFLOAT3& hit);
    bool WorldToTerrainUV(const XMFLOAT3& hit, XMFLOAT2& uv);
    void ApplyBrushAtUV(const XMFLOAT2& uv, float dt, ID3D12GraphicsCommandList* cmd);
    void UploadRegionToGPU(int x0, int y0, int w, int h, ID3D12GraphicsCommandList* cmd);


    void ApplyTAAToIntermediate(
        D3D12_GPU_DESCRIPTOR_HANDLE currSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE historySrv,
        D3D12_GPU_DESCRIPTOR_HANDLE prevDepthSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE currDepthSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE velocitySrv,
        ID3D12Resource* dst,
        D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
        D3D12_GPU_DESCRIPTOR_HANDLE& outSrv);
    void DoTAAPass(
        D3D12_GPU_DESCRIPTOR_HANDLE currLDR,
        D3D12_GPU_DESCRIPTOR_HANDLE& outSrv,
        bool writeToHistoryA);
    void UpdateTAACB();
    void CopyDepthToPrev();
    void TransitionResource(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res, D3D12_RESOURCE_STATES& current, D3D12_RESOURCE_STATES target);

    void BuildRaytracingAS();
    void UpdateRaytracingTLAS();
    void BuildBLAS_Once(ID3D12Device5* device5, ID3D12GraphicsCommandList4* cmd4);
    void BuildOrUpdateTLAS(ID3D12Device5* device5, ID3D12GraphicsCommandList4* cmd4, bool update);

    void InitAlphaShadowDemoResources();
    void UpdateAlphaShadowCB();
    void UpdateGrassSrvHandle();

    void EnsureMotionBlurResources();
    void UpdateMotionBlurCB();
    void ApplyMotionBlurToIntermediate(
        D3D12_GPU_DESCRIPTOR_HANDLE colorSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSrv,
        const DirectX::XMMATRIX& prevViewProj,
        const DirectX::XMMATRIX& invViewProj,
        ID3D12Resource* dst,
        D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
        D3D12_GPU_DESCRIPTOR_HANDLE& outSrv);

};
