#pragma once
#include <DirectXMath.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_set>

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

inline bool Intersects(const AABB& a, const AABB& b) 
{
    return !(a.maxv.x < b.minv.x || a.minv.x > b.maxv.x ||
        a.maxv.y < b.minv.y || a.minv.y > b.maxv.y ||
        a.maxv.z < b.minv.z || a.minv.z > b.maxv.z);
}

inline bool IntersectsFrustum(const AABB& b, const XMFLOAT4 planes[6]) 
{
    for (int i = 0; i < 6; ++i) 
    {
        const XMFLOAT4& pl = planes[i];
        XMFLOAT3 p = {
            (pl.x >= 0 ? b.maxv.x : b.minv.x),
            (pl.y >= 0 ? b.maxv.y : b.minv.y),
            (pl.z >= 0 ? b.maxv.z : b.minv.z)
        };
        const float dist = pl.x * p.x + pl.y * p.y + pl.z * p.z + pl.w;
        if (dist < 0.0f) return false;
    }
    return true;
}

struct OctItem 
{
    AABB box;
    void* ptr;
};

class Octree 
{
public:
    void Build(const AABB& sceneBounds,
        const std::vector<OctItem>& items,
        int maxDepth = 8, int capacity = 8, float minNodeSize = 1.0f)
    {
        m_maxDepth = maxDepth; m_capacity = capacity; m_minSize = minNodeSize;
        m_root = std::make_unique<Node>(); m_root->bounds = sceneBounds;
        for (const auto& it : items) insert(m_root.get(), it, 0);
    }

    void QueryFrustum(const XMFLOAT4 planes[6], std::vector<void*>& out) const 
    {
        out.clear(); m_seen.clear(); queryFrustum(m_root.get(), planes, out);
    }

    void QueryBox(const AABB& box, std::vector<void*>& out) const 
    {
        out.clear(); m_seen.clear(); queryBox(m_root.get(), box, out);
    }

private:
    struct Node 
    {
        AABB bounds;
        std::vector<OctItem> items;
        std::unique_ptr<Node> ch[8];

        bool isLeaf() const 
        {
            for (int i = 0; i < 8; ++i) if (ch[i]) return false;
            return true;
        }
    };

    std::unique_ptr<Node> m_root;
    int m_maxDepth = 8, m_capacity = 8;
    float m_minSize = 1.0f;
    mutable std::unordered_set<void*> m_seen;

    static int childIndex(const AABB& parent, const AABB& obj)
    {
        const XMFLOAT3 c = parent.center();
        int idx = 0;
        auto fitsAxis = [&](float omin, float omax, float mid)->int {
            if (omax <= mid) return 0; if (omin >= mid) return 1; return -1;
            };
        int x = fitsAxis(obj.minv.x, obj.maxv.x, c.x);
        int y = fitsAxis(obj.minv.y, obj.maxv.y, c.y);
        int z = fitsAxis(obj.minv.z, obj.maxv.z, c.z);
        if (x < 0 || y < 0 || z < 0) return -1;
        idx |= x ? 1 : 0; idx |= y ? 2 : 0; idx |= z ? 4 : 0;
        return idx;
    }

    static AABB childBounds(const AABB& p, int idx) 
    {
        const XMFLOAT3 c = p.center();
        AABB b{}; b.minv = p.minv; b.maxv = p.maxv;
        if (idx & 1) b.minv.x = c.x; else b.maxv.x = c.x;
        if (idx & 2) b.minv.y = c.y; else b.maxv.y = c.y;
        if (idx & 4) b.minv.z = c.z; else b.maxv.z = c.z;
        return b;
    }

    void insert(Node* n, const OctItem& it, int depth) 
    {
        const XMFLOAT3 sz = n->bounds.size();
        if (depth >= m_maxDepth || (int)n->items.size() < m_capacity ||
            sz.x <= m_minSize || sz.y <= m_minSize || sz.z <= m_minSize)
        {
            n->items.push_back(it); return;
        }
        int ci = childIndex(n->bounds, it.box);
        if (ci < 0) 
        {
            n->items.push_back(it); return; 
        }

        if (!n->ch[ci]) 
        {
            n->ch[ci] = std::make_unique<Node>();
            n->ch[ci]->bounds = childBounds(n->bounds, ci);
        }
        insert(n->ch[ci].get(), it, depth + 1);
    }

    void queryFrustum(const Node* n, const XMFLOAT4 planes[6], std::vector<void*>& out) const 
    {
        if (!n) return;
        if (!IntersectsFrustum(n->bounds, planes)) return;
        for (const auto& it : n->items) 
        {
            if (m_seen.insert(it.ptr).second) 
            {
                if (IntersectsFrustum(it.box, planes)) out.push_back(it.ptr);
            }
        }
        for (int i = 0; i < 8; ++i) queryFrustum(n->ch[i].get(), planes, out);
    }

    void queryBox(const Node* n, const AABB& box, std::vector<void*>& out) const 
    {
        if (!n) return;
        if (!Intersects(n->bounds, box)) return;
        for (const auto& it : n->items) 
        {
            if (m_seen.insert(it.ptr).second) 
            {
                if (Intersects(it.box, box)) out.push_back(it.ptr);
            }
        }
        for (int i = 0; i < 8; ++i) queryBox(n->ch[i].get(), box, out);
    }
};