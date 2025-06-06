#pragma once
#include <string>
#include <DirectXMath.h>

struct Material {
    std::string name;
    std::wstring albedoTexPath;
    std::wstring normalTexPath;
    std::wstring metallicRoughTexPath;
    DirectX::XMFLOAT3 diffuseColor;
    DirectX::XMFLOAT3 specularColor;
    float shininess;
    UINT albedoSrvIndex = UINT_MAX;
    UINT normalSrvIndex = UINT_MAX;
    UINT metallicRoughSrvIndex = UINT_MAX;
};