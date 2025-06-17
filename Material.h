#pragma once
#include <string>
#include <DirectXMath.h>

class DX12Framework;
namespace DirectX { class ResourceUploadBatch; }

struct Material {
    DirectX::XMFLOAT3 ambient = { 0.1f, 0.1f, 0.1f };  // Ka
    DirectX::XMFLOAT3 diffuse = { 0.8f, 0.8f, 0.8f };  // Kd
    DirectX::XMFLOAT3 specular = { 1.0f, 1.0f, 1.0f };  // Ks
    float             shininess = 32.0f;               // Ns
    std::string       diffuseTexPath;                  // map_Kd
};

