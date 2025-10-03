#pragma once
#include <cfloat>
#include <memory>
#include <vector>
#include <array>
#include <algorithm>
#include <DirectXMath.h>
#include "AABB.h"

using namespace DirectX;

inline bool IntersectsFrustumAABB(const AABB& b, const XMFLOAT4 planes[6]) 
{
    for (int i = 0; i < 6; ++i) 
    {
        const auto& pl = planes[i];
        XMFLOAT3 p = 
        {
            (pl.x >= 0 ? b.maxv.x : b.minv.x),
            (pl.y >= 0 ? b.maxv.y : b.minv.y),
            (pl.z >= 0 ? b.maxv.z : b.minv.z)
        };
        float d = pl.x * p.x + pl.y * p.y + pl.z * p.z + pl.w;
        if (d < 0.0f) return false;
    }
    return true;
}

struct TerrainNode
{
    AABB bounds;
    int  level = 0;        
    XMFLOAT2 uv0{ 0,0 };
    XMFLOAT2 uv1{ 1,1 }; 
    XMFLOAT3 origin{ 0,0,0 };
    float size = 0.0f;       
    std::unique_ptr<TerrainNode> ch[4];
    bool isLeaf() const { return !(ch[0] || ch[1] || ch[2] || ch[3]); }
};

class QuadTree
{
public:

    void Build(const DirectX::XMFLOAT3& worldOrigin, float worldSize, int maxDepth) 
    {
        m_maxDepth = maxDepth;
        m_root = std::make_unique<TerrainNode>();
        m_root->level = 0;
        m_root->origin = worldOrigin; 
        m_root->size = worldSize;
        m_root->uv0 = DirectX::XMFLOAT2(0, 0); 
        m_root->uv1 = DirectX::XMFLOAT2(1, 1);
        computeBounds(*m_root, m_heightMax);
        subdivide(m_root.get(), maxDepth);
    }

    void SetHeightMax(float h) { m_heightMax = h; }
    TerrainNode* Root() const { return m_root.get(); }

    void CollectLOD(const DirectX::XMFLOAT3& camPos,
        float screenTau,
        const DirectX::XMFLOAT4X4& viewProj,
        const DirectX::XMFLOAT4 planes[6],
        std::vector<TerrainNode*>& out)
    {
        out.clear();
        traverseLOD(m_root.get(), camPos, screenTau, viewProj, planes, out);
    }

    void CollectLOD(const DirectX::XMFLOAT3& camPos,
        const DirectX::XMFLOAT4X4& viewProj,
        const DirectX::XMFLOAT4 planes[6],
        const int* lodGridRes, int lodCount,
        int viewportW, int viewportH,
        float pxPerVertexTarget,            
        std::vector<TerrainNode*>& out);

private:
    std::unique_ptr<TerrainNode> m_root;
    float m_heightMax = 200.0f;
    int m_maxDepth = 0;

    void computeBounds(TerrainNode& n, float hmax) 
    {
        n.bounds = {};
        n.bounds.expand({ n.origin.x, 0.0f, n.origin.z });
        n.bounds.expand({ n.origin.x + n.size, 0.0f, n.origin.z + n.size });
        n.bounds.expand({ n.origin.x, hmax, n.origin.z });
        n.bounds.expand({ n.origin.x + n.size, hmax, n.origin.z + n.size });
    }

    void subdivide(TerrainNode* n, int maxDepth)
    {
        if (n->level >= maxDepth) return;
        float hs = n->size * 0.5f;
        for (int i = 0; i < 4; ++i)
        {
            n->ch[i] = std::make_unique<TerrainNode>();
            auto* c = n->ch[i].get();
            c->level = n->level + 1;
            c->size = hs;
            c->origin = n->origin;
            if (i & 1) c->origin.x += hs;
            if (i & 2) c->origin.z += hs;

            c->uv1 = DirectX::XMFLOAT2(n->uv1.x * 0.5f, n->uv1.y * 0.5f);

            float stepU = (i & 1) ? 0.5f : 0.0f;
            float stepV = (i & 2) ? 0.5f : 0.0f;
            c->uv0 = DirectX::XMFLOAT2(n->uv0.x + stepU * n->uv1.x,
                n->uv0.y + stepV * n->uv1.y);

            computeBounds(*c, m_heightMax);
            subdivide(c, maxDepth);
        }
    }

    bool projectedTooLarge(const TerrainNode* n,
        const DirectX::XMFLOAT4X4& viewProj,
        float screenTau)
    {
        using namespace DirectX;

        XMFLOAT3 p0{ n->origin.x,           0.0f, n->origin.z };
        XMFLOAT3 p1{ n->origin.x + n->size, 0.0f, n->origin.z };
        XMFLOAT3 p2{ n->origin.x,           0.0f, n->origin.z + n->size };
        XMFLOAT3 p3{ n->origin.x + n->size, 0.0f, n->origin.z + n->size };

        const XMMATRIX VP = XMLoadFloat4x4(&viewProj);

        auto toNdc = [&](const XMFLOAT3& P, float& x, float& y)
            {
                XMVECTOR v = XMVectorSet(P.x, P.y, P.z, 1.0f);
                v = XMVector4Transform(v, VP);
                const float w = XMVectorGetW(v);
                if (w == 0.0f) { x = y = 0.0f; return; }
                x = XMVectorGetX(v) / w;
                y = XMVectorGetY(v) / w;
            };

        float x0, y0, x1, y1, x2, y2, x3, y3;
        toNdc(p0, x0, y0); toNdc(p1, x1, y1);
        toNdc(p2, x2, y2); toNdc(p3, x3, y3);

        const float minx = min(min(x0, x1), min(x2, x3));
        const float maxx = max(max(x0, x1), max(x2, x3));
        const float miny = min(min(y0, y1), min(y2, y3));
        const float maxy = max(max(y0, y1), max(y2, y3));

        const float spanX = maxx - minx;
        const float spanY = maxy - miny;
        const float ndcSpan = max(spanX, spanY);

        return (ndcSpan * 0.5f) > screenTau;
    }

    void traverseLOD(TerrainNode* n, const XMFLOAT3& camPos,
        float screenTau, const XMFLOAT4X4& viewProj,
        const XMFLOAT4 planes[6],
        std::vector<TerrainNode*>& out)
    {
        if (!IntersectsFrustumAABB(n->bounds, planes)) return;

        if (n->level >= m_maxDepth || !projectedTooLarge(n, viewProj, screenTau))
        {
            out.push_back(n);
            return;
        }
        for (auto& ch : n->ch)
            if (ch) traverseLOD(ch.get(), {}, screenTau, viewProj, planes, out);
    }
};
