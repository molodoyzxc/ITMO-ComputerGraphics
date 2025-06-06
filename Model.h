#pragma once

#include <vector>
#include <DirectXMath.h>
#include "MeshPart.h"
#include "Material.h"

struct Model {
    // Все части, из которых состоит OBJ-модель.
    // После парсинга OBJ-файла у нас будет N MeshPart, каждый со своей геометрией и materialID.
    std::vector<MeshPart> parts;

    // Целый список материалов (из .mtl) – соответствует порядку, в котором они шли в MTL-файле
    std::vector<Material> materials;

    // Трансформация всей модели
    DirectX::XMFLOAT3 position = { 0,0,0 };
    DirectX::XMFLOAT3 rotation = { 0,0,0 };
    DirectX::XMFLOAT3 scale = { 1,1,1 };

    DirectX::XMMATRIX GetWorldMatrix() const {
        using namespace DirectX;
        return XMMatrixScaling(scale.x, scale.y, scale.z)
            * XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z)
            * XMMatrixTranslation(position.x, position.y, position.z);
    }
};
