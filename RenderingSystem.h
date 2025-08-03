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
    Timer timer;

    std::vector<SceneObject> m_objects;
    std::vector<SceneObject*> m_visibleObjects;
    std::vector<Light> lights;

    ComPtr<ID3D12Resource> m_constantBuffer;
    ComPtr<ID3D12Resource> m_lightBuffer;
    ComPtr<ID3D12Resource> m_ambientBuffer;
    ComPtr<ID3D12Resource> m_tessBuffer;
    uint8_t* m_pTessCbData = nullptr;

    float m_cameraX, m_cameraY, m_cameraZ;
    float m_lightX, m_lightY, m_lightZ;
    float m_yaw, m_pitch;

    void KeyboardControl();
    void SetObjects();
    void SetLights();
    void LoadTextures();
    void CountFPS();
    void LoadErrorTextures();
};
