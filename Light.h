#pragma once
#include <DirectXMath.h>
using namespace DirectX;

struct Light
{
    int type = 1; // Directional = 0, Point = 1, Spot = 2
    XMFLOAT3 position{ 0, 5, 0, };
    XMFLOAT3 direction{ 0, -1, 0, };
    XMFLOAT3 spotDirection{ 1.0f, 0.0f, 0.0f, };
    XMFLOAT3 color{ 1.0f, 1.0f, 1.0f, };
    float radius = 50.0f;
    float inner = 15.0f;
    float outer = 30.0f;
     
    Light() = default;

    float innerCone() { return cosf(XMConvertToRadians(inner)); };
    float outerCone() { return cosf(XMConvertToRadians(outer)); };

    XMMATRIX GetWorldMatrix() const {
        XMMATRIX T = XMMatrixTranslation(position.x, position.y, position.z);
        return T;
    }

    XMVECTOR GetDirectionVector() const {
        return XMVector3Normalize(XMLoadFloat3(&direction));
    }
};
