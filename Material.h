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
    std::string normalTexPath; // bump / map_Bump / map_Normal
    std::string displacementTexPath; // disp / map_disp
    std::string roughnessTexPath; // map_Pr
    std::string metallicTexPath; // map_Pm
    std::string aoTexPath; // map_Ka
};

