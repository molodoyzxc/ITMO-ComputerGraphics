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
    float m_fakeCameraZ = 0.0f;

    float m_currentFPS = 0.0f;
    float dt;

    XMFLOAT3 direction = {50.0f, -50.0f, 0.0f};
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
    bool m_enableTonemap = true;
    bool m_enableGamma = true;
    bool m_enableVignette = true;

    struct IBLSet
    {
        UINT irradianceSrv = UINT(-1);
        UINT prefilteredSrv = UINT(-1);
        UINT brdfSrv = UINT(-1);
        CD3DX12_GPU_DESCRIPTOR_HANDLE tableStart{};
    };
    IBLSet m_ibl;

    std::unique_ptr<Octree> m_octree;

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
};
