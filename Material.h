#pragma once
#include <string>
#include <DirectXMath.h>

class DX12Framework;
namespace DirectX { class ResourceUploadBatch; }

struct Material {
    DirectX::XMFLOAT3 ambient = { 1.0f, 1.0f, 1.0f }; // Ka
    DirectX::XMFLOAT3 diffuse = { 0.5f, 0.5f, 0.5f }; // Kd
    DirectX::XMFLOAT3 specular = { 0.5f, 0.5f, 0.5f }; // Ks
    float roughness = 0.5f;
    float metallic = 0.0f;
    float ao = 1.0f;
    float shininess = 512.0f; // Ns
    std::string diffuseTexPath; // map_Kd
    std::string normalTexPath; // bump / map_Bump / map_Normal
    std::string displacementTexPath; // disp / map_disp
    std::string roughnessTexPath; // map_Pr
    std::string metallicTexPath; // map_Pm
    std::string aoTexPath; // map_Ka
};

