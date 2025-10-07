cbuffer ObjectCB : register(b0)
{
    row_major float4x4 World;
    row_major float4x4 ViewProj;
    float4 UVScaleBias;
};

cbuffer MaterialCB : register(b4)
{
    float useNormalMap;
    uint diffuseIdx;
    uint normalIdx;
    uint dispIdx;
    
    uint roughIdx;
    uint metalIdx;
    uint aoIdx;
    uint heightDeltaIdx;
    
    uint hasDiffuseMap;
    uint hasRoughMap;
    uint hasMetalMap;
    uint hasAOMap;
    
    float4 baseColor;
    
    float roughnessValue;
    float metallicValue;
    float aoValue;
    float _padM;
};


static const uint MAX_SRV = 100;
Texture2D<float4> gTextures[MAX_SRV] : register(t0);
SamplerState samLinear : register(s0);

struct VSIn
{
    float3 pos : POSITION; 
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0; 
    float3 tangent : TANGENT;
    float handed : HAND;
};

struct VSOut
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

float SampleHeight(float2 uv)
{
    float hBase = gTextures[dispIdx].SampleLevel(samLinear, uv, 0).r;
    float hDelta = gTextures[heightDeltaIdx].SampleLevel(samLinear, uv, 0).r;
    return hBase + hDelta;
}

VSOut VS_TerrainGBuffer(VSIn IN)
{
    VSOut OUT;
    
    float2 uvGlobal = IN.uv * UVScaleBias.xy + UVScaleBias.zw;
    
    float h = SampleHeight(uvGlobal) * baseColor.a;

    float3 local = float3(IN.pos.x, IN.pos.y + h, IN.pos.z);
    float4 wp = mul(float4(local, 1), World);
    
    OUT.worldPos = wp;
    OUT.posH = mul(wp, ViewProj);

    OUT.normal = normalize(mul(IN.normal, (float3x3) World));
    OUT.tangent = normalize(mul(IN.tangent, (float3x3) World));
    OUT.handed = IN.handed;
    
    OUT.uv = uvGlobal;
    return OUT;
}

GBufferOut PS_TerrainGBuffer(VSOut IN)
{
    GBufferOut OUT;
    float2 uv = IN.uv;
    
    float3 Ngeo = normalize(cross(ddy(IN.worldPos.xyz), ddx(IN.worldPos.xyz)));
    
    float3 N = Ngeo;
    if (useNormalMap != 0.0f)
    {
        float3 nMap = gTextures[normalIdx].Sample(samLinear, uv).xyz * 2.0 - 1.0;
        nMap.y = -nMap.y;
        float3 T = normalize(IN.tangent);
        float3 B = normalize(cross(Ngeo, T)) * IN.handed;
        float3 mapped = normalize(nMap.x * T + nMap.y * B + nMap.z * Ngeo);
        N = mapped;
    }

    float3 base;

    if (hasDiffuseMap != 0)
    {
        base = gTextures[diffuseIdx].Sample(samLinear, uv).rgb;
    }
    else
    {
        float h01 = gTextures[dispIdx].SampleLevel(samLinear, uv, 0).r;
        
        const float waterLevel = 0.12;
        const float grassLevel = 0.4; 
        const float rockLevel = 0.7; 
        
        const float blend = 0.08; 
        
        const float3 colWater = float3(0.05, 0.15, 0.25);
        const float3 colGrass = float3(0.18, 0.35, 0.14);
        const float3 colRock = float3(0.36, 0.35, 0.34);
        const float3 colSnow = float3(0.92, 0.94, 0.96);
        
        float wWater = 1.0 - smoothstep(waterLevel, waterLevel + blend, h01);
        float wGrass = smoothstep(waterLevel, waterLevel + blend, h01)
                     * (1.0 - smoothstep(grassLevel, grassLevel + blend, h01));
        float wRock = smoothstep(grassLevel, grassLevel + blend, h01)
                     * (1.0 - smoothstep(rockLevel, rockLevel + blend, h01));
        float wSnow = smoothstep(rockLevel, rockLevel + blend, h01);
        
        float slope = saturate((1.0 - Ngeo.y - 0.2) / 0.6);
        float slopeMaskByHeight = smoothstep(grassLevel, rockLevel, h01);
        float slopeBoost = slope * slopeMaskByHeight * 0.6;
        wRock = saturate(wRock + slopeBoost);
        
        float wSum = wWater + wGrass + wRock + wSnow + 1e-6;
        wWater /= wSum;
        wGrass /= wSum;
        wRock /= wSum;
        wSnow /= wSum;
        
        base = wWater * colWater
         + wGrass * colGrass
         + wRock * colRock
         + wSnow * colSnow;
        
        if (h01 < waterLevel)
        {
            float depth = saturate((waterLevel - h01) / max(waterLevel, 1e-6));
            base = lerp(base, colWater * 0.7, depth * 0.5);
        }
        
        base = float3(0.8,0.8,0.8);
    }

    OUT.Albedo = float4(base, 1.0);
    OUT.Normal = float4(N * 0.5 + 0.5, 0);

    float rough = (hasRoughMap != 0) ? gTextures[roughIdx].Sample(samLinear, uv).r : roughnessValue;
    float metal = (hasMetalMap != 0) ? gTextures[metalIdx].Sample(samLinear, uv).r : metallicValue;
    float ao = (hasAOMap != 0) ? gTextures[aoIdx].Sample(samLinear, uv).r : aoValue;
    
    ao = saturate(ao * lerp(1.0, 0.85, smoothstep(0.2, 0.8, 1.0 - abs(Ngeo.y))));

    OUT.Params = float4(saturate(rough), saturate(metal), saturate(ao), 0.0);
    return OUT;
}

