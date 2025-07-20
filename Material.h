#pragma once
#include <string>
#include <DirectXMath.h>

class DX12Framework;
namespace DirectX { class ResourceUploadBatch; }

struct Material {
    DirectX::XMFLOAT3 ambient = { 1.0f, 1.0f, 1.0f }; // Ka
    DirectX::XMFLOAT3 diffuse = { 1.0f, 1.0f, 1.0f }; // Kd
    DirectX::XMFLOAT3 specular = { 0.8f, 0.8f, 0.8f }; // Ks
    float shininess = 512.0f; // Ns
    std::string diffuseTexPath; // map_Kd
};

