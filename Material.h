#pragma once
#include <string>
#include <DirectXMath.h>

struct Material {
    DirectX::XMFLOAT4 diffuseColor;   // fallback, если нет текстуры
    std::wstring        diffuseTexName;// путь к файлу (может быть пустым)
    uint32_t srvIndex = 0;      // куда положили SRV в heap
    XMFLOAT2   uvTiling = { 1,1 };
    XMFLOAT2   uvOffset = { 0,0 };

};