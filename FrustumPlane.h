#pragma once
#include <DirectXMath.h>

struct FrustumPlane {
    DirectX::XMVECTOR normal;
};

void ExtractFrustumPlanes(DirectX::XMFLOAT4 planes[6], const DirectX::XMMATRIX& viewProj)
{
    using namespace DirectX;

    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, viewProj);

    // Левая
    XMStoreFloat4(&planes[0], XMPlaneNormalize(XMVectorSet(
        m._14 + m._11,
        m._24 + m._21,
        m._34 + m._31,
        m._44 + m._41)));

    // Правая
    XMStoreFloat4(&planes[1], XMPlaneNormalize(XMVectorSet(
        m._14 - m._11,
        m._24 - m._21,
        m._34 - m._31,
        m._44 - m._41)));

    // Нижняя
    XMStoreFloat4(&planes[2], XMPlaneNormalize(XMVectorSet(
        m._14 + m._12,
        m._24 + m._22,
        m._34 + m._32,
        m._44 + m._42)));

    // Верхняя
    XMStoreFloat4(&planes[3], XMPlaneNormalize(XMVectorSet(
        m._14 - m._12,
        m._24 - m._22,
        m._34 - m._32,
        m._44 - m._42)));

    // Ближняя
    XMStoreFloat4(&planes[4], XMPlaneNormalize(XMVectorSet(
        m._13,
        m._23,
        m._33,
        m._43)));

    // Дальняя
    XMStoreFloat4(&planes[5], XMPlaneNormalize(XMVectorSet(
        m._14 - m._13,
        m._24 - m._23,
        m._34 - m._33,
        m._44 - m._43)));
}