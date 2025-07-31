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
    float3 ctr = (w0 + w1 + w2) / 3.0f;
    float d = distance(ctr, cameraPos);
    
    float t = saturate((d - minDist) / (maxDist - minDist));
    float f = lerp(maxTess, minTess, t);

    PatchConstantData pcd;
    pcd.edges[0] = f;
    pcd.edges[1] = f;
    pcd.edges[2] = f;
    pcd.inside = f;
    return pcd;
}

[domain("tri")]
DSOutput DSMain(
    PatchConstantData pcd,
    const OutputPatch<HSOutput, 3> patch,
    float3 bary : SV_DomainLocation)
{
    DSOutput o;
    
    float3 pos = patch[0].worldPos * bary.x
                  + patch[1].worldPos * bary.y
                  + patch[2].worldPos * bary.z;
    float3 normal = normalize(
                      patch[0].worldNormal * bary.x
                    + patch[1].worldNormal * bary.y
                    + patch[2].worldNormal * bary.z);
    float3 tangent = normalize(
                      patch[0].worldT * bary.x
                    + patch[1].worldT * bary.y
                    + patch[2].worldT * bary.z);
    float handedness = patch[0].handedness;

    float2 uv = patch[0].uv * bary.x
              + patch[1].uv * bary.y
              + patch[2].uv * bary.z;
    
    float height = gDisplacementMap.SampleLevel(samLinear, uv, 0).r * heightScale;
    pos += normal * height;
    
    float4 clip = mul(ViewProj, float4(pos, 1.0f));
    clip.y *= -1.0f;

    o.posH = clip;
    o.uv = uv;
    o.worldPos = float4(pos, 1.0f);
    o.normal = normal;
    o.tangent = tangent;
    o.handed = handedness;
    return o;
}

float4 PSMain(DSOutput IN) : SV_TARGET
{
    float3 N = normalize(IN.normal);
    float3 T = normalize(IN.tangent);
    float3 B = cross(N, T) * IN.handed;
    
    float3 nMap = gNormalMap.SampleLevel(samLinear, IN.uv, 0).xyz * 2.0f - 1.0f;
    float3 worldN = normalize(nMap.x * T + nMap.y * B + nMap.z * N);
    
    float3 lightDir = normalize(float3(0.0f, 1.0f, 0.0f));
    float diff = saturate(dot(worldN, lightDir));
    float4 albedo = gDiffuseMap.SampleLevel(samLinear, IN.uv, 0);

    return albedo * diff;
}
