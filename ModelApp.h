#pragma once
#include "Pipeline.h"
#include "InputDevice.h"
#include "Meshes.h"
#include <wrl/client.h>
#include <d3d12.h>
#include <vector>
#include "SceneObject.h"
#include "IGameApp.h"

using Microsoft::WRL::ComPtr;

class DX12Framework;

class ModelApp : public IGameApp
{
public:
    ModelApp(DX12Framework* framework, InputDevice* input);

    void Initialize() override;
    void Update(float dt) override;
    void Render() override;

private:
    DX12Framework* m_framework;
    InputDevice* m_input;
    Pipeline       m_pipeline;

    std::vector<SceneObject> m_objects;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;

    float m_cameraX, m_cameraY, m_cameraZ;
    float m_lightX, m_lightY, m_lightZ;
    float m_viewX, m_viewY, m_viewZ;
    float m_yaw, m_pitch;
    XMFLOAT4 m_cubeColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};
