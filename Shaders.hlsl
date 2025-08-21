cbuffer ObjectCB : register(b0)
{
    row_major float4x4 World;
    row_major float4x4 ViewProj;
};

cbuffer LightingCB : register(b1)
{
    int LightType;
    float3 pad0;

    float4 LightDir;
    float4 LightColor;

    float4 LightPosRange;

    float4 SpotDirInnerCos;
    float4 SpotOuterPad;

    row_major float4x4 InvViewProj;
    float4 ScreenSize;
    
    row_major float4x4 LightViewProj[4];
    float4 CascadeSplits;
    row_major float4x4 View;
    float4 ShadowParams;
};

cbuffer AmbientCB : register(b2)
{
    float4 AmbientColor;
};

cbuffer MaterialCB : register(b4)
{
    float useNormalMap; // 0 – geometry, 1 – normal map
    uint diffuseIdx;
    uint normalIdx;
    uint dispIdx;
    uint roughIdx;
    uint metalIdx;
    uint aoIdx;
    float pad[1];
};

static const uint MAX_SRV = 100; // DX12Framework::srvDesc.NumDescriptors
Texture2D<float4> gTextures[MAX_SRV] : register(t0);

Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gParamTex : register(t2);
Texture2D<float> gDepthTex : register(t3);
Texture2DArray<float> gShadowMap : register(t4);

SamplerState samLinear : register(s0);
SamplerComparisonState samShadow : register(s1);

struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 tangent : TANGENT;
    float handed : HAND;
};

struct VSOutput
{
    float4 posH : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 worldPos : TEXCOORD1;
    float3 tangent : TANGENT;
    float handed : HAND;
};

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Params : SV_Target2;
};

struct VSShadowOut
{
    float4 posH : SV_POSITION;
};

VSShadowOut VS_Shadow(VSInput IN)
{
    VSShadowOut OUT;
    float4 wp = mul(float4(IN.pos, 1.0), World);
    OUT.posH = mul(wp, ViewProj);
    return OUT;
}

float3 ProjectToShadowTex(float3 worldPos, uint cascadeIdx)
{
    float4 lp = mul(float4(worldPos, 1.0), LightViewProj[cascadeIdx]);
    if (lp.w <= 0.0)
        return float3(-1.0, -1.0, 0.0);
    lp.xyz /= lp.w;
    float2 uv = lp.xy * float2(0.5, -0.5) + 0.5;
    return float3(uv, lp.z);
}

float PCF_Shadow(float3 worldPos, uint cascadeIdx)
{
    float3 suvz = ProjectToShadowTex(worldPos, cascadeIdx);
    float2 uv = suvz.xy;
    float z = suvz.z;

    if (any(uv < 0.0) || any(uv > 1.0))
        return 1.0;

    float texel = ShadowParams.x;
    float bias = ShadowParams.y;

    float sum = 0.0;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            float2 o = float2(dx, dy) * texel;
            sum += gShadowMap.SampleCmpLevelZero(samShadow, float3(uv + o, cascadeIdx), z - bias);
        }
    }
    return sum / 9.0;
}

uint ChooseCascade(float3 worldPos)
{
    float3 viewPos = mul(float4(worldPos, 1.0), View).xyz;
    float vz = viewPos.z;
    uint count = (uint) ShadowParams.z;

    uint ci = 0;
    ci += (vz > CascadeSplits.x) ? 1u : 0u;
    ci += (vz > CascadeSplits.y) ? 1u : 0u;
    ci += (vz > CascadeSplits.z) ? 1u : 0u;
    ci = min(ci, count - 1u);
    return ci;
}

VSOutput VS_GBuffer(VSInput IN)
{
    VSOutput OUT;
    float4 wp = mul(float4(IN.pos, 1.0), World);
    OUT.worldPos = wp;
    OUT.posH = mul(wp, ViewProj);
    OUT.normal = normalize(mul(IN.normal, (float3x3) World));
    OUT.uv = IN.uv;
    OUT.tangent = normalize(mul(IN.tangent, (float3x3) World));
    OUT.handed = IN.handed;
    return OUT;
}

GBufferOut PS_GBuffer(VSOutput IN)
{
    GBufferOut OUT;
    float2 uv = IN.uv;
    float4 albedo = gTextures[diffuseIdx].Sample(samLinear, uv);
    clip(albedo.a - 0.1);

    OUT.Albedo = albedo;

    float3 nMap = gTextures[normalIdx].Sample(samLinear, uv).xyz * 2.0 - 1.0;
    nMap.y = -nMap.y;

    float3 N = normalize(IN.normal);
    float3 T = normalize(IN.tangent);
    float3 B = cross(N, T) * IN.handed;
    float3 mapped = normalize(nMap.x * T + nMap.y * B + nMap.z * N);
    float3 worldN = lerp(N, mapped, useNormalMap);

    OUT.Normal = float4(worldN * 0.5 + 0.5, 0);
    OUT.Params = float4(gTextures[roughIdx].Sample(samLinear, uv).r,
    gTextures[metalIdx].Sample(samLinear, uv).r,
    gTextures[aoIdx].Sample(samLinear, uv).r,
    0);
    return OUT;
}

struct VSFwdOut
{
    float4 posH : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

VSFwdOut VSMain(VSInput IN)
{
    VSFwdOut OUT;
    float4 wp = mul(float4(IN.pos, 1.0), World);
    OUT.posH = mul(wp, ViewProj);
    OUT.normal = normalize(mul(IN.normal, (float3x3) World));
    OUT.uv = IN.uv;
    return OUT;
}

float4 PSMain(VSFwdOut IN) : SV_TARGET
{
    float3 N = normalize(IN.normal);
    float3 L = normalize(-LightDir.xyz);
    float ndotl = max(dot(N, L), 0.0);
    return float4(LightColor.rgb * ndotl, 1.0);
}

struct VSQOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSQOut VS_Quad(uint id : SV_VertexID)
{
    VSQOut OUT;
    float2 verts[6] =
    {
        float2(-1, -1),
        float2(-1, 1),
        float2(1, 1),
        
        float2(-1, -1),
        float2(1, 1),
        float2(1, -1),
    };
    OUT.pos = float4(verts[id], 0, 1);
    OUT.uv = verts[id] * float2(0.5, -0.5) + 0.5;
    return OUT;
}

float4 PS_Ambient(VSQOut IN) : SV_TARGET
{
    float4 albedo = gAlbedoTex.Sample(samLinear, IN.uv);
    if (albedo.a < 0.1)
        discard;
    return float4(albedo.rgb * AmbientColor.rgb, 1.0);
}

float4 PS_Lighting(VSQOut IN) : SV_TARGET
{
    float2 uv = IN.uv;
    float depth = gDepthTex.SampleLevel(samLinear, uv, 0).r;
    float2 ndc;
    ndc.x = 2.0 * uv.x - 1.0;
    ndc.y = 1.0 - 2.0 * uv.y;
    
    float4 clipPos = float4(ndc.x, ndc.y, depth, 1.0);
    float4 worldH = mul(clipPos, InvViewProj);
    float3 worldPos = worldH.xyz / worldH.w;
    float4 albedo = gAlbedoTex.Sample(samLinear, uv);
    float shadow;
    if (albedo.a < 0.1)
    {
        discard;
    }
    float3 normalSample = gNormalTex.Sample(samLinear, uv).xyz;
    float3 worldNormal = normalize(normalSample * 2.0 - 1.0);
    float3 result = float3(0, 0, 0);
    if (LightType == 0)  // Directional
    {
        float3 Ldir = normalize(-LightDir.xyz);
        float nL = max(dot(worldNormal, Ldir), 0.0);
        uint cascadeIdx = ChooseCascade(worldPos);
        shadow = PCF_Shadow(worldPos, cascadeIdx);
        //uint c = ChooseCascade(worldPos);
        //float3 debugC = (c == 0) ? float3(1, 0, 0) : (c == 1) ? float3(0, 1, 0) : (c == 2) ? float3(0, 0, 1) : float3(1, 1, 0);
        //result += debugC * 0.5;
        result += albedo.rgb * LightColor.rgb * nL * shadow;
    }
    else if (LightType == 1)  // Point
    {
        float3 toLight = LightPosRange.xyz - worldPos;
        float dist = length(toLight);
        
        if (dist < LightPosRange.w && dist > 0.01)
        {
            float3 Ldir = normalize(toLight);
            float nL = max(dot(worldNormal, Ldir), 0.0);
            float att = saturate(1.0 - (dist / LightPosRange.w));
            att = att * att;
            result += albedo.rgb * LightColor.rgb * nL * att;
        }
    }
    else if (LightType == 2)  // Spot
    {
        float3 toLight = LightPosRange.xyz - worldPos;
        float dist = length(toLight);
        
        if (dist < LightPosRange.w && dist > 0.01)
        {
            float3 Ldir = normalize(toLight);
            float nL = max(dot(worldNormal, Ldir), 0.0);
            float distAtt = saturate(1.0 - (dist / LightPosRange.w));
            distAtt = distAtt * distAtt;
            float3 coneAxis = normalize(SpotDirInnerCos.xyz);
            float3 lightToFrag = -Ldir;
            float cosA = dot(lightToFrag, coneAxis);
            float spotAtt = smoothstep(SpotOuterPad.x, SpotDirInnerCos.w, cosA);
            result += albedo.rgb * LightColor.rgb * nL * distAtt * spotAtt;
        }
    }

    return float4(result, 1.0);
}