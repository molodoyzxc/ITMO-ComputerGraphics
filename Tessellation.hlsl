cbuffer ObjectCB : register(b0)
{
    float4x4 World;
    float4x4 ViewProj;
};

cbuffer TessCB : register(b3)
{
    float3 cameraPos;
    float heightScale;
    float minDist;
    float maxDist;
    float minTess;
    float maxTess;
};

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);
SamplerState samLinear : register(s0);

struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 tangent : TANGENT;
    float handedness : HAND;
};

struct VSOutput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 tangent : TANGENT;
    float handedness : HAND;
};

struct HSOutput
{
    float3 worldPos : POSITION;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldT : TANGENT;
    float handedness : HAND;
};

struct PatchConstantData
{
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
};

struct DSOutput
{
    float4 posH : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 worldPos : TEXCOORD1;
    float3 tangent : TANGENT;
    float handed : HAND;
};

VSOutput VSMain(VSInput IN)
{
    VSOutput o;
    o.pos = IN.pos;
    o.normal = IN.normal;
    o.uv = IN.uv;
    o.tangent = IN.tangent;
    o.handedness = IN.handedness;
    return o;
}

[domain("tri")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstants")]
HSOutput HSMain(InputPatch<VSOutput, 3> patch, uint cpID : SV_OutputControlPointID)
{
    HSOutput o;
    o.worldPos = mul(World, float4(patch[cpID].pos, 1.0f)).xyz;
    o.worldNormal = normalize(mul((float3x3) World, patch[cpID].normal));
    o.uv = patch[cpID].uv;
    o.worldT = normalize(mul((float3x3) World, patch[cpID].tangent));
    o.handedness = patch[cpID].handedness;
    return o;
}

PatchConstantData PatchConstants(InputPatch<VSOutput, 3> patch)
{
    float3 w0 = mul(World, float4(patch[0].pos, 1)).xyz;
    float3 w1 = mul(World, float4(patch[1].pos, 1)).xyz;
    float3 w2 = mul(World, float4(patch[2].pos, 1)).xyz;

    PatchConstantData pcd;
    float d, t;
  
    d = distance((w0 + w1) * 0.5, cameraPos);
    t = saturate((d - minDist) / (maxDist - minDist));
    pcd.edges[0] = lerp(maxTess, minTess, t);
    
    d = distance((w1 + w2) * 0.5, cameraPos);
    t = saturate((d - minDist) / (maxDist - minDist));
    pcd.edges[1] = lerp(maxTess, minTess, t);
    
    d = distance((w2 + w0) * 0.5, cameraPos);
    t = saturate((d - minDist) / (maxDist - minDist));
    pcd.edges[2] = lerp(maxTess, minTess, t);
    
    pcd.inside = (pcd.edges[0] + pcd.edges[1] + pcd.edges[2]) / 3;
    return pcd;
}

[domain("tri")]
DSOutput DSMain(
    PatchConstantData pcd,
    const OutputPatch<HSOutput, 3> patch,
    float3 bary : SV_DomainLocation)
{
    DSOutput o;
    
    float3 worldPos = patch[0].worldPos * bary.x
                    + patch[1].worldPos * bary.y
                    + patch[2].worldPos * bary.z;
    
    float3 worldNormal = normalize(
                           patch[0].worldNormal * bary.x
                         + patch[1].worldNormal * bary.y
                         + patch[2].worldNormal * bary.z);
    
    float3 worldTangent = normalize(
                            patch[0].worldT * bary.x
                          + patch[1].worldT * bary.y
                          + patch[2].worldT * bary.z);
    
    float handedness = patch[0].handedness * bary.x
                     + patch[1].handedness * bary.y
                     + patch[2].handedness * bary.z;
    
    float2 uv = patch[0].uv * bary.x
              + patch[1].uv * bary.y
              + patch[2].uv * bary.z;
    
    float height = gDisplacementMap.SampleLevel(samLinear, uv, 0).r * heightScale;
    worldPos += worldNormal * height;
    
    float4 clipPos = mul(ViewProj, float4(worldPos, 1.0f));
    clipPos.y *= -1.0f;
    
    o.posH = clipPos;
    o.normal = worldNormal;
    o.uv = uv;
    o.worldPos = float4(worldPos, 1.0f);
    o.tangent = worldTangent;
    o.handed = handedness;
    
    return o;
}

float4 PSMain(DSOutput IN) : SV_TARGET
{
    return float4(1, 0, 1, 1);
}
