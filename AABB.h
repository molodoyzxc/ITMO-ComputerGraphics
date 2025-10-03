#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct AABB
{
    XMFLOAT3 minv{ FLT_MAX,FLT_MAX,FLT_MAX };
    XMFLOAT3 maxv{ -FLT_MAX,-FLT_MAX,-FLT_MAX };

    void expand(const AABB& b)
    {
        minv.x = min(minv.x, b.minv.x); minv.y = min(minv.y, b.minv.y); minv.z = min(minv.z, b.minv.z);
        maxv.x = max(maxv.x, b.maxv.x); maxv.y = max(maxv.y, b.maxv.y); maxv.z = max(maxv.z, b.maxv.z);
    }

    void expand(const XMFLOAT3& p)
    {
        minv.x = min(minv.x, p.x); minv.y = min(minv.y, p.y); minv.z = min(minv.z, p.z);
        maxv.x = max(maxv.x, p.x); maxv.y = max(maxv.y, p.y); maxv.z = max(maxv.z, p.z);
    }

    XMFLOAT3 center() const
    {
        return { (minv.x + maxv.x) * 0.5f,(minv.y + maxv.y) * 0.5f,(minv.z + maxv.z) * 0.5f };
    }

    XMFLOAT3 size()   const
    {
        return { (maxv.x - minv.x),(maxv.y - minv.y),(maxv.z - minv.z) };
    }

    bool empty() const
    {
        return (minv.x > maxv.x) || (minv.y > maxv.y) || (minv.z > maxv.z);
    }
};