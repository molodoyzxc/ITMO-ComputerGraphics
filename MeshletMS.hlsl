struct MeshVertex
{
    float3 pos;
    float3 normal;
    float2 uv;
    float3 tangent;
    float handed;
};

struct Meshlet
{
    uint vertexOffset;
    uint vertexCount;
    uint primOffset;
    uint primCount;
};

StructuredBuffer<MeshVertex> gMeshVertices : register(t0, space2);
StructuredBuffer<Meshlet> gMeshlets : register(t1, space2);
StructuredBuffer<uint> gMeshletVertices : register(t2, space2);
StructuredBuffer<uint> gMeshletPrims : register(t3, space2);

struct MSOut
{
    float4 posH : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 worldPos : TEXCOORD1;
    float3 tangent : TANGENT;
    float handed : HAND;
};

uint3 UnpackTri(uint packed)
{
    uint i0 = (packed) & 0xFFu;
    uint i1 = (packed >> 8) & 0xFFu;
    uint i2 = (packed >> 16) & 0xFFu;
    return uint3(i0, i1, i2);
}

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void MS_GBuffer(
    uint3 groupId : SV_GroupID,
    uint tid : SV_GroupIndex,
    out vertices MSOut outVerts[64],
    out indices uint3 outTris[126])
{
    Meshlet m = gMeshlets[groupId.x];
    
    SetMeshOutputCounts(m.vertexCount, m.primCount);

    if (tid < m.vertexCount)
    {
        uint vIdx = gMeshletVertices[m.vertexOffset + tid];
        MeshVertex v = gMeshVertices[vIdx];

        MSOut OUT;
        float4 wp = mul(float4(v.pos, 1.0), World);
        OUT.worldPos = wp;
        OUT.posH = mul(wp, ViewProj);

        OUT.normal  = normalize(mul(v.normal,  (float3x3)World));
        OUT.tangent = normalize(mul(v.tangent, (float3x3)World));
        OUT.uv      = v.uv;
        OUT.handed  = v.handed;

        outVerts[tid] = OUT;
    }

    if (tid < m.primCount)
    {
        uint packed = gMeshletPrims[m.primOffset + tid];
        outTris[tid] = UnpackTri(packed);
    }
}
