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

    uint8_t* m_pCbData = nullptr;
    uint8_t* m_pLightData = nullptr;
    uint8_t* m_pAmbientData = nullptr;
    uint8_t* m_pTessCbData = nullptr;
    uint8_t* m_pMaterialData = nullptr;
    uint8_t* m_pShadowCbData = nullptr;

    XMMATRIX view, proj, viewProj;
    ID3D12GraphicsCommandList* cmd;
    XMFLOAT4X4 m_lightViewProj;

    float m_yaw = 0.f;
    float m_pitch = 0.f;
    XMFLOAT3 cameraPos{ 0.0f, 0.0f, 0.0f };
    float m_near = 0.1f;
    float m_far = 50000.0f;

    float cameraSpeed = 3.0f;
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

    XMFLOAT3 direction = {-1.0f, -2.0f, -1.0f};
    static constexpr UINT CSM_CASCADES = 4;
    XMFLOAT4X4 m_lightViewProjCSM[CSM_CASCADES];
    float m_cascadeSplits[CSM_CASCADES];
    std::array<std::vector<SceneObject*>, CSM_CASCADES> m_shadowCasters;
    std::array<float, CSM_CASCADES> m_biasPerCascade{};

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
};
