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
    
    float4 ShadowMaskParams;
    
    uint FrameIndex;
    float3 _padFrame;
};

cbuffer AmbientCB : register(b2)
{
    float4 AmbientColor;
    float4 FogColorDensity;
    float4 FogParams;
    float4 SunParams;
};

cbuffer AnimCB : register(b3)
{
    float Time; 
    float Amplitude;
    float Explode;
    uint Mode;

    float Frequency;
    float3 _padAnim;
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

    float4 baseColor;
    
    uint useRoughMap;
    uint useMetalMap;
    uint useAOMap;
    uint hasRoughMap;

    uint hasMetalMap;
    uint hasAOMap;
    uint _padM0;
    uint _padM1;
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

TextureCube<float3> gIrradiance : register(t5);
TextureCube<float3> gPrefEnv : register(t6);
Texture2D<float2> gBRDFLUT : register(t7);

Texture2D gShadowMask : register(t8);

RaytracingAccelerationStructure gScene : register(t9);

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

    OUT.Albedo = float4(1.0, 0.0, 1.0, 1.0);
    OUT.Normal = float4(N * 0.5 + 0.5, 0.0);
    OUT.Params = float4(1.0, 0.0, 1.0, 0.0);

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
    
    const float ROUGH_DEFAULT = 1.0;
    const float METAL_DEFAULT = 0.0;
    const float AO_DEFAULT = 1.0;
    
    const bool useR = (useRoughMap != 0) && (hasRoughMap != 0);
    const bool useM = (useMetalMap != 0) && (hasMetalMap != 0);
    const bool useA = (useAOMap != 0) && (hasAOMap != 0);
    
    float rough = useR ? gTextures[roughIdx].Sample(samLinear, uv).r : ROUGH_DEFAULT;
    float metal = useM ? gTextures[metalIdx].Sample(samLinear, uv).r : METAL_DEFAULT;
    float ao = useA ? gTextures[aoIdx].Sample(samLinear, uv).r : AO_DEFAULT;

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

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(1.0.xxx - roughness, F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 ApplyHeightFog(float3 color, float3 worldPos)
{
    float3 fogColor = FogColorDensity.rgb;
    float density = FogColorDensity.a;
    float heightFalloff = FogParams.x;
    float baseHeight = FogParams.y;
    float maxOpacity = FogParams.z;
    float enabled = FogParams.w;
    
    if (enabled < 0.5 || density <= 0.0)
        return color;

    float3 toCamera = CameraPos.xyz - worldPos;
    float dist = length(toCamera);
    if (dist <= 0.0)
        return color;
    
    float h = max(worldPos.y - baseHeight, 0.0);
    float heightTerm = exp(-heightFalloff * h);
    
    float opticalDepth = density * heightTerm * dist;
    
    float transmittance = exp(-opticalDepth);

    float fogAmount = 1.0 - transmittance;
    fogAmount = saturate(fogAmount * maxOpacity);

    return lerp(color, fogColor, fogAmount);
}

float4 PS_Ambient(VSQOut IN) : SV_TARGET
{
    float2 uv = IN.uv;
    
    int2 pix = int2(uv * ScreenSize.xy);
    pix = clamp(pix, int2(0, 0), int2((int) ScreenSize.x - 1, (int) ScreenSize.y - 1));

    float2 uvCenter = (float2(pix) + 0.5) / ScreenSize.xy;
    
    float depth = gDepthTex.Load(int3(pix, 0)).r;
    if (depth >= 1.0)
        discard;
    
    float2 ndc = float2(uvCenter.x * 2.0 - 1.0, 1.0 - uvCenter.y * 2.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldH = mul(clipPos, InvViewProj);
    float3 worldPos = worldH.xyz / worldH.w;
    
    float4 baseColor = gAlbedoTex.Load(int3(pix, 0));
    if (baseColor.a < 0.1)
        discard;
    
    float3 N = normalize(gNormalTex.Load(int3(pix, 0)).xyz * 2.0 - 1.0);
    float3 params = gParamTex.Load(int3(pix, 0)).rgb;
    
    float roughness = saturate(params.r);
    float metallic = saturate(params.g);
    float ao = saturate(params.b);

    float3 V = normalize(CameraPos.xyz - worldPos);
    float NoV = saturate(dot(N, V));
    
    float3 F0 = lerp(0.04.xxx, baseColor, metallic);
    
    float3 diffuseIBL = gIrradiance.Sample(samLinear, N) * baseColor;
    
    uint w, h, mips;
    gPrefEnv.GetDimensions(0, w, h, mips);
    float3 R = reflect(-V, N);
    float lod = roughness * (mips - 1);
    float3 prefiltered = gPrefEnv.SampleLevel(samLinear, R, lod);

    float2 brdf = gBRDFLUT.Sample(samLinear, float2(NoV, roughness));
    float3 specularIBL = prefiltered * (F0 * brdf.x + brdf.y);
    
    float3 kS = FresnelSchlickRoughness(NoV, F0, roughness);
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    
    float3 color = (kD * diffuseIBL + specularIBL) * ao * AmbientColor.rgb;

    color = ApplyHeightFog(color, worldPos);
    
    return float4(color, 1.0);
}

uint HashU32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint ReverseBits32(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
    bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
    return bits;
}

float RadicalInverse_VdC(uint bits)
{
    return (ReverseBits32(bits) * 2.3283064365386963e-10);
}

float2 Hammersley(uint i, uint N)
{
    return float2((i + 0.5) / (float) N, RadicalInverse_VdC(i));
}

float Rand01(inout uint s)
{
    s = HashU32(s);
    return (s & 0x00FFFFFFu) / 16777216.0;
}

float3 OffsetRay(float3 p, float3 n)
{
    const float intScale = 256.0f;
    const float floatScale = 1.0f / 65536.0f;
    const float origin = 1.0f / 32.0f;

    int3 of = int3(intScale * n);

    float3 pI;
    pI.x = asfloat(asint(p.x) + ((p.x < 0.0f) ? -of.x : of.x));
    pI.y = asfloat(asint(p.y) + ((p.y < 0.0f) ? -of.y : of.y));
    pI.z = asfloat(asint(p.z) + ((p.z < 0.0f) ? -of.z : of.z));

    float3 pF;
    pF.x = (abs(p.x) < origin) ? p.x + floatScale * n.x : pI.x;
    pF.y = (abs(p.y) < origin) ? p.y + floatScale * n.y : pI.y;
    pF.z = (abs(p.z) < origin) ? p.z + floatScale * n.z : pI.z;

    return pF;
}

void BuildONB(float3 n, out float3 b1, out float3 b2)
{
    float sign = (n.z >= 0.0) ? 1.0 : -1.0;
    float a = -1.0 / (sign + n.z);
    float b = n.x * n.y * a;
    b1 = float3(1.0 + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = float3(b, sign + n.y * n.y * a, -n.y);
}

float3 SampleCone(float3 axis, float cosThetaMax, float u1, float u2)
{
    float cosTheta = lerp(1.0, cosThetaMax, u1);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = 6.2831853 * u2;

    float3 b1, b2;
    BuildONB(axis, b1, b2);

    return normalize(b1 * (cos(phi) * sinTheta) +
                     b2 * (sin(phi) * sinTheta) +
                     axis * cosTheta);
}

cbuffer AlphaShadowCB : register(b6)
{
    uint GrassInstanceID;
    uint GrassUsesXZ;
    float GrassAlphaCutoff;
    float _pad0;

    float2 GrassUvScale;
    float2 GrassUvOffset;
}

Texture2D<float4> gGrassTex : register(t30);

float TraceShadow(float3 origin, float3 dir, float tMax, float3 worldPos)
{
    RayQuery < RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > q;

    float distToCam = length(worldPos - CameraPos.xyz);
    float tmin = 1;

    RayDesc ray;
    ray.Origin = origin;
    ray.TMin = tmin;
    ray.Direction = dir;
    ray.TMax = tMax;

    q.TraceRayInline(gScene, 0, 0xFF, ray);

    while (q.Proceed())
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            uint instID = q.CandidateInstanceID();

            if (instID == GrassInstanceID)
            {
                float t = q.CandidateTriangleRayT();
                float3 hitW = origin + dir * t;
                
                float4 hitObj4 = mul(float4(hitW, 1.0), q.CandidateWorldToObject3x4());
                float3 hitObj = hitObj4.xyz;

                float2 baseUV = (GrassUsesXZ != 0) ? hitObj.xz : hitObj.xy;

                float2 uv = baseUV * GrassUvScale + GrassUvOffset;
                uv = frac(uv);

                float a = gGrassTex.SampleLevel(samLinear, uv, 0).a;
                
                if (a >= GrassAlphaCutoff)
                {
                    q.CommitNonOpaqueTriangleHit();
                }
            }
            else
            {
                q.CommitNonOpaqueTriangleHit();
            }
        }
    }

    return (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
}

float RTSoftShadow_Directional(float2 uv, float3 worldPos, float3 N, float3 L)
{
    const uint SAMPLES = 4;
    float cone = ShadowMaskParams.w;

    int2 pix = int2(uv * ScreenSize.xy);
    uint seed = (uint(pix.x) * 1973u) ^ (uint(pix.y) * 9277u) ^ (FrameIndex * 26699u) ^ 0x68bc21ebu;
    
    float2 rot = float2(Rand01(seed), Rand01(seed));

    float cosThetaMax = cos(cone);

    float3 origin = OffsetRay(worldPos, N);
    float tMax = 10000.0;

    float sum = 0.0;
    [unroll]
    for (uint i = 0; i < SAMPLES; ++i)
    {
        float2 xi = frac(Hammersley(i, SAMPLES) + rot);
        float3 dir = SampleCone(L, cosThetaMax, xi.x, xi.y);
        sum += TraceShadow(origin, dir, tMax, worldPos);
    }
    return sum / (float) SAMPLES;
}

float4 PS_Lighting(VSQOut IN) : SV_TARGET
{
    float2 uv = IN.uv;
    
    int2 pix = int2(uv * ScreenSize.xy);
    pix = clamp(pix, int2(0, 0), int2((int) ScreenSize.x - 1, (int) ScreenSize.y - 1));
    
    float2 uvCenter = (float2(pix) + 0.5) / ScreenSize.xy;
    
    float depth = gDepthTex.Load(int3(pix, 0)).r;
    if (depth >= 1.0)
        discard;
    
    float2 ndc = float2(uvCenter.x * 2.0 - 1.0, 1.0 - uvCenter.y * 2.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldH = mul(clipPos, InvViewProj);
    float3 worldPos = worldH.xyz / worldH.w;
    
    float4 albedoTex = gAlbedoTex.Load(int3(pix, 0));
    if (albedoTex.a < 0.1)
        discard;

    float3 N = normalize(gNormalTex.Load(int3(pix, 0)).xyz * 2.0 - 1.0);
    float3 params = gParamTex.Load(int3(pix, 0)).rgb;
    
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

        if (ShadowParams.w > 0.5)
            shadow = RTSoftShadow_Directional(uv, worldPos, N, L);
        else
            shadow = PCF_Shadow(worldPos, cascadeIdx);
        
        {
            float2 tiling = ShadowMaskParams.xy;
            float strength = saturate(ShadowMaskParams.z); 
            
            float3 w = normalize(-LightDir.xyz);
            float3 a = (abs(w.y) < 0.999) ? float3(0, 1, 0) : float3(1, 0, 0);
            float3 u = normalize(cross(a, w));
            float3 v = cross(w, u);
            
            float2 muv = float2(dot(worldPos, u), dot(worldPos, v)) * tiling;
            
            muv.y = -muv.y;
            
            float2 muvWrap = frac(muv);
            float mask = gShadowMask.Sample(samLinear, muvWrap).r;
            
            float shadowAmount = 1.0 - shadow;
            
            float shadowInFull = lerp(0.0, mask, strength);
            
            float shadowDecor = lerp(1.0, shadowInFull, shadowAmount);

            shadow = shadowDecor;
        }
        
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metallic);

        float denom = max(4.0 * saturate(dot(N, V)) * NdotL, 1e-7);
        float3 spec = (D * G * F) / denom;
        
        float3 diff = (kD * baseColor / PI) * ao;

        radiance = LightColor.rgb * (diff + spec) * NdotL * shadow;
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

            float3 diff = (kD * baseColor / PI) * ao;
            
            radiance = LightColor.rgb * (diff + spec) * NdotL * att;
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

            float3 diff = (kD * baseColor / PI) * ao;
            
            radiance = LightColor.rgb * (diff + spec) * NdotL * distAtt * spotAtt;
        }
    }
    
    float3 color = ApplyHeightFog(radiance, worldPos);

    return float4(color, 1.0);
}

float3 ComputeSkyColor(float3 dir)
{
    float dayFactor = SunParams.x;
    float sunsetFactor = SunParams.y;
    float nightFactor = SunParams.z;

    float t = saturate(dir.y * 0.5 + 0.5);

    float3 dayZenith = float3(0.1, 0.35, 0.9);
    float3 dayHorizon = float3(0.7, 0.85, 1.0);
    float3 nightZenith = float3(0.005, 0.005, 0.012);
    float3 nightHorizon = float3(0.02, 0.02, 0.06);

    float3 daySky = lerp(dayHorizon, dayZenith, t);
    float3 nightSky = lerp(nightHorizon, nightZenith, t);
    float3 baseSky = lerp(nightSky, daySky, dayFactor);
    
    float3 sunsetHorizon = float3(1.8, 0.45, 0.15);
    float3 sunsetZenith = float3(1.2, 0.3, 0.2);
    float3 sunsetSky = lerp(sunsetHorizon, sunsetZenith, t);

    float3 sky = lerp(baseSky, sunsetSky, sunsetFactor);
    
    float horizonGlow = pow(1.0 - abs(dir.y), 2.8) * sunsetFactor * 3.0;
    sky += float3(1.8, 0.4, 0.1) * horizonGlow;
    
    float3 sunDir = normalize(-LightDir.xyz);
    float sunDot = saturate(dot(dir, sunDir));
    float sunDisk = pow(sunDot, 1024.0) * 15.0;
    float sunGlow = pow(sunDot, 6.0) * dayFactor * 2.0;
    float3 sunColor = lerp(float3(1.0, 0.95, 0.85), float3(1.8, 0.4, 0.1), sunsetFactor);
    sky += sunColor * (sunDisk + sunGlow);
    
    float brightness = lerp(0.15, 1.6, dayFactor + sunsetFactor * 0.7);
    sky *= brightness;

    return sky;
}

float4 PS_Skybox(VSQOut IN) : SV_TARGET
{
    float depth = gDepthTex.SampleLevel(samLinear, IN.uv, 0).r;
    if (depth < 1.0)
        discard;
    
    float2 ndc = float2(2.0 * IN.uv.x - 1.0, 1.0 - 2.0 * IN.uv.y);
    float4 farH = mul(float4(ndc, 1.0, 1.0), InvViewProj);
    float3 worldFar = farH.xyz / max(farH.w, 1e-6);
    float3 dir = normalize(worldFar - CameraPos.xyz);

    float3 sky = ComputeSkyColor(dir);

    return float4(sky, 1.0);
}

cbuffer PreviewCB : register(b0)
{
    int PreviewMode;
    float PreviewNear;
    float PreviewFar;
    float PreviewPad;
};


float4 PS_PreviewGBuffer(VSQOut IN) : SV_TARGET
{
    float2 uv = IN.uv;

    float4 albedo = gAlbedoTex.Sample(samLinear, uv);

    float4 normal = gNormalTex.Sample(samLinear, uv);

    float4 params = gParamTex.Sample(samLinear, uv);

    float depth = gDepthTex.Sample(samLinear, uv).r;

    float3 color = 0.0.xxx;

    if (PreviewMode == 0)
    { 
        color = albedo.rgb;
    }
    else if (PreviewMode == 1)
    {
        color = normal.xyz;
    }
    else if (PreviewMode == 2)
    { 
        float linearDepth = PreviewNear * PreviewFar / (PreviewFar + depth * (PreviewNear - PreviewFar));
        float normDepth = saturate(linearDepth / PreviewFar); 
        color = normDepth.xxx;
    }
    else if (PreviewMode == 3)
    {
        color = params.rrr;
    }
    else if (PreviewMode == 4)
    { 
        color = params.ggg;
    }
    else if (PreviewMode == 5)
    { 
        color = params.bbb;
    }
   
    return float4(color, 1.0);
}

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

        float3 p = v.pos;

        if (Mode == 0)
        {
            float s = 1.0 + Explode * Amplitude * sin(Time);
            p *= s;
        }
        else if (Mode == 1)
        {
            uint h = HashU32(groupId.x * 9781u + 6271u);
            float3 dir;
            dir.x = ((h & 1023u) / 511.5f) - 1.0f;
            dir.y = (((h >> 10) & 1023u) / 511.5f) - 1.0f;
            dir.z = (((h >> 20) & 1023u) / 511.5f) - 1.0f;
            dir = normalize(dir);

            float t = 0.5 + 0.5 * sin(Time);
            p += dir * (Explode * Amplitude) * t;
        }
        else if (Mode == 2)
        {
            float phase = dot(p, float3(0.0, 1.0, 0.0)) * Frequency;
            float w = sin(Time + phase);
            p += v.normal * (Explode * Amplitude) * w;
        }
        
        MSOut OUT;
        float4 wp = mul(float4(p, 1.0), World);
        OUT.worldPos = wp;
        OUT.posH = mul(wp, ViewProj);

        OUT.normal = normalize(mul(v.normal, (float3x3) World));
        OUT.tangent = normalize(mul(v.tangent, (float3x3) World));
        OUT.uv = v.uv;
        OUT.handed = v.handed;

        outVerts[tid] = OUT;
    }

    if (tid < m.primCount)
    {
        uint packed = gMeshletPrims[m.primOffset + tid];
        outTris[tid] = UnpackTri(packed);
    }
}
