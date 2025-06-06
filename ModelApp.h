#pragma once
#include "DX12Framework.h"
#include "Pipeline.h"
#include "Model.h"
#include "ModelLoader.h"
#include "InputDevice.h"
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
    Pipeline      m_pipeline;
    
    Model m_model;

    ComPtr<ID3D12Resource> m_constantBuffer;
    float m_cameraX, m_cameraY, m_cameraZ;
    float m_lightX, m_lightY, m_lightZ;
    float m_viewX, m_viewY, m_viewZ;
    float m_yaw, m_pitch;
    XMFLOAT4 m_objectColor = { 1.0f,1.0f,1.0f,1.0f };
};
