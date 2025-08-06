cbuffer ObjectCB : register(b0)
{
    float4x4 World;
    float4x4 ViewProj;
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

    column_major float4x4 InvViewProj;
    float4 ScreenSize;
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

Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gParamTex : register(t2);
Texture2D<float> gDepthTex : register(t3);

static const uint MAX_SRV = 100; // DX12Framework::srvDesc.NumDescriptors
Texture2D<float4> gTextures[MAX_SRV] : register(t0);
SamplerState samLinear : register(s0);

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
    float4 WorldPos : SV_Target3;
};

VSOutput VS_GBuffer(VSInput IN)
{
    VSOutput OUT;
    float4 wp = mul(World, float4(IN.pos, 1));
    OUT.worldPos = wp;
    OUT.posH = mul(ViewProj, wp);
    OUT.posH.y *= -1;
    OUT.normal = normalize(mul((float3x3) World, IN.normal));
    OUT.uv = IN.uv;
    OUT.tangent = normalize(mul((float3x3) World, IN.tangent));
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
    OUT.WorldPos = IN.worldPos;
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
    float4 worldPos = mul(World, float4(IN.pos, 1.0));
    OUT.posH = mul(ViewProj, worldPos);
    OUT.posH.y *= -1;
    OUT.normal = normalize(mul((float3x3) World, IN.normal));
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
    OUT.uv = verts[id] * 0.5 + 0.5;
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
    int2 pix = int2(uv * ScreenSize.xy);
    float depth = gDepthTex.Load(int3(pix, 0));
    float2 ndc;
    ndc.x = uv.x * 2.0 - 1.0;
    ndc.y = -((1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc.x, ndc.y, depth, 1.0);
    float4 worldH = mul(clipPos, InvViewProj);
    float3 worldPos = worldH.xyz / worldH.w;
    float4 albedo = gAlbedoTex.Sample(samLinear, uv);
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
        result += albedo.rgb * LightColor.rgb * nL;
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