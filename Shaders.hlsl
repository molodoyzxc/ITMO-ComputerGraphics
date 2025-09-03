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

    float4 CameraPos;
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
    uint hasDiffuseMap;

    float4 baseColor; // Kd
    
    float roughnessValue;
    float metallicValue;
    float aoValue;
    uint hasRoughMap;

    uint hasMetalMap;
    uint hasAOMap;
    float _padM;
};

cbuffer PostCB : register(b1) 
{
    float Exposure;
    float Gamma;
    float VignetteStrength;
    float VignettePower;
    float2 VignetteCenter;
    float2 InvResolution;
    int Tonemap;
    int _padPost;
}

static const uint MAX_SRV = 100; // DX12Framework::srvDesc.NumDescriptors
Texture2D<float4> gTextures[MAX_SRV] : register(t0);

Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gParamTex : register(t2);
Texture2D<float> gDepthTex : register(t3);
Texture2DArray<float> gShadowMap : register(t4);

SamplerState samLinear : register(s0);
SamplerComparisonState samShadow : register(s1);

TextureCube<float3> gIrradiance : register(t5);
TextureCube<float3> gPrefEnv : register(t6);
Texture2D<float2> gBRDFLUT : register(t7);

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

struct Particle
{
    float3 pos;
    float3 vel;
    float age;
    float lifetime;
    float size;
};

StructuredBuffer<Particle> gParticles : register(t0, space1);

VSOutput VS_GBufferParticle(VSInput IN, uint instanceId : SV_InstanceID)
{
    Particle P = gParticles[instanceId];
    
    float3 local = IN.pos * P.size + P.pos;

    VSOutput OUT;
    float4 wp = mul(float4(local, 1.0), World);
    OUT.worldPos = wp;
    OUT.posH = mul(wp, ViewProj);

    float3 N = normalize(mul(IN.normal, (float3x3) World));
    float3 T = normalize(mul(IN.tangent, (float3x3) World));
    OUT.normal = N;
    OUT.uv = IN.uv;
    OUT.tangent = T;
    OUT.handed = IN.handed;
    return OUT;
}

GBufferOut PS_GBufferParticle(VSOutput IN)
{
    GBufferOut OUT;
    float3 N = normalize(IN.normal);

    OUT.Albedo = float4(1.0, 0.6, 0.2, 1.0);
    OUT.Normal = float4(N * 0.5 + 0.5, 0.0);
    OUT.Params = float4(0.5, 0.0, 1.0, 0.0);

    return OUT;
}

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
    
    float4 texAlbedo = (hasDiffuseMap != 0) ? gTextures[diffuseIdx].Sample(samLinear, uv) : float4(1, 1, 1, 1);
    float4 albedo = float4(baseColor.rgb, 1.0) * texAlbedo;
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
    
    float rough = (hasRoughMap != 0) ? gTextures[roughIdx].Sample(samLinear, uv).r : roughnessValue;
    float metal = (hasMetalMap != 0) ? gTextures[metalIdx].Sample(samLinear, uv).r : metallicValue;
    float ao = (hasAOMap != 0) ? gTextures[aoIdx].Sample(samLinear, uv).r : aoValue;

    OUT.Params = float4(saturate(rough), saturate(metal), saturate(ao), 0.0);
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
    float2 verts[3] =
    {
        float2(-1, -1),
        float2(-1, 3),
        float2(3, -1),
        
    };
    OUT.pos = float4(verts[id], 0, 1);
    OUT.uv = verts[id] * float2(0.5, -0.5) + 0.5;
    return OUT;
}

static float3 TonemapReinhard(float3 x)
{
    return x / (1.0 + x);
}

static float3 TonemapACES(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PS_Post(VSQOut IN) : SV_TARGET
{
    float3 hdr = gAlbedoTex.Sample(samLinear, IN.uv).rgb;
    
    hdr *= Exposure;
    
    float3 mapped = hdr;
    if (Tonemap == 1)
        mapped = TonemapReinhard(hdr);
    else if (Tonemap == 2)
        mapped = TonemapACES(hdr);
    else
        mapped = saturate(hdr);
    
    float invGamma = 1.0 / max(Gamma, 1e-4);
    float3 srgb = pow(saturate(mapped), invGamma);
    
    float2 aspect = float2(InvResolution.y / InvResolution.x, 1.0);
    float2 d = (IN.uv - VignetteCenter) * aspect;
    float vig = 1.0 - VignetteStrength * pow(saturate(length(d) * 1.41421356), VignettePower);

    return float4(srgb * vig, 1.0);
}

static const float PI = 3.14159265359;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-7);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-7);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float4 PS_Ambient(VSQOut IN) : SV_TARGET
{
    float4 albedo = gAlbedoTex.Sample(samLinear, IN.uv);
    if (albedo.a < 0.1)
        discard;

    float ao = gParamTex.Sample(samLinear, IN.uv).b;
    float3 ambient = albedo.rgb * AmbientColor.rgb * ao;
    return float4(ambient, 1.0);
}

float4 PS_Lighting(VSQOut IN) : SV_TARGET
{
    float2 uv = IN.uv;
    
    float depth = gDepthTex.SampleLevel(samLinear, uv, 0).r;
    float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    float4 clipPos = float4(ndc.x, ndc.y, depth, 1.0);
    float4 worldH = mul(clipPos, InvViewProj);
    float3 worldPos = worldH.xyz / worldH.w;
    
    float4 albedoTex = gAlbedoTex.Sample(samLinear, uv);
    if (albedoTex.a < 0.1)
        discard;

    float3 N = normalize(gNormalTex.Sample(samLinear, uv).xyz * 2.0 - 1.0);
    float3 params = gParamTex.Sample(samLinear, uv).rgb;
    float roughness = saturate(params.r);
    float metallic = saturate(params.g);
    float ao = saturate(params.b);

    float3 V = normalize(CameraPos.xyz - worldPos);
    float3 baseColor = saturate(albedoTex.rgb);
    
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    float3 radiance = 0.0.xxx;
    float NdotL = 0.0;
    float shadow = 1.0;

    if (LightType == 0)  // Directional
    {
        float3 L = normalize(-LightDir.xyz);
        float3 H = normalize(V + L);
        NdotL = saturate(dot(N, L));

        uint cascadeIdx = ChooseCascade(worldPos);
        shadow = PCF_Shadow(worldPos, cascadeIdx);

        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metallic);

        float denom = max(4.0 * saturate(dot(N, V)) * NdotL, 1e-7);
        float3 spec = (D * G * F) / denom;

        radiance = LightColor.rgb * (kD * baseColor / PI + spec) * NdotL * shadow;
    }
    else if (LightType == 1)  // Point
    {
        float3 toLight = LightPosRange.xyz - worldPos;
        float dist = length(toLight);
        if (dist < LightPosRange.w && dist > 0.01)
        {
            float3 L = toLight / dist;
            float3 H = normalize(V + L);
            NdotL = saturate(dot(N, L));
            
            float att = saturate(1.0 - (dist / LightPosRange.w));
            att *= att;

            float D = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

            float3 kS = F;
            float3 kD = (1.0 - kS) * (1.0 - metallic);

            float denom = max(4.0 * saturate(dot(N, V)) * NdotL, 1e-7);
            float3 spec = (D * G * F) / denom;

            radiance = LightColor.rgb * (kD * baseColor / PI + spec) * NdotL * att;
        }
    }
    else if (LightType == 2)  // Spot
    {
        float3 toLight = LightPosRange.xyz - worldPos;
        float dist = length(toLight);
        if (dist < LightPosRange.w && dist > 0.01)
        {
            float3 L = toLight / dist;
            float3 H = normalize(V + L);
            NdotL = saturate(dot(N, L));

            float distAtt = saturate(1.0 - (dist / LightPosRange.w));
            distAtt *= distAtt;

            float3 coneAxis = normalize(SpotDirInnerCos.xyz);
            float3 lightToFrag = -L;
            float cosA = dot(lightToFrag, coneAxis);
            float spotAtt = smoothstep(SpotOuterPad.x, SpotDirInnerCos.w, cosA);

            float D = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

            float3 kS = F;
            float3 kD = (1.0 - kS) * (1.0 - metallic);

            float denom = max(4.0 * saturate(dot(N, V)) * NdotL, 1e-7);
            float3 spec = (D * G * F) / denom;

            radiance = LightColor.rgb * (kD * baseColor / PI + spec) * NdotL * distAtt * spotAtt;
        }
    }

    return float4(radiance, 1.0);
}