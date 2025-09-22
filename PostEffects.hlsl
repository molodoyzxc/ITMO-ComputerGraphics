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
    
    float Saturation;
    float PosterizeLevels;
    float PixelateSize;
    float _pad2;
}

Texture2D gAlbedoTex : register(t0);
SamplerState samLinear : register(s0);

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
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PS_CopyHDRtoLDR(VSQOut IN) : SV_TARGET
{
    float3 hdr = gAlbedoTex.Sample(samLinear, IN.uv).rgb;
    float3 ldr = saturate(hdr);
    return float4(ldr, 1.0);
}

float4 PS_CopyLDR(VSQOut IN) : SV_TARGET
{
    float3 ldr = gAlbedoTex.Sample(samLinear, IN.uv).rgb;
    return float4(ldr, 1.0);
}

float4 PS_Tonemap(VSQOut IN) : SV_TARGET
{
    float3 hdr = gAlbedoTex.Sample(samLinear, IN.uv).rgb;
    hdr *= Exposure;

    float3 mapped;
    if (Tonemap == 1)
        mapped = TonemapReinhard(hdr);
    else if (Tonemap == 2)
        mapped = TonemapACES(hdr);
    else
        mapped = saturate(hdr);

    return float4(mapped, 1.0);
}

float4 PS_Gamma(VSQOut IN) : SV_TARGET
{
    float3 ldr = gAlbedoTex.Sample(samLinear, IN.uv).rgb;
    float invGamma = 1.0 / max(Gamma, 1e-4);
    float3 srgb = pow(saturate(ldr), invGamma);
    return float4(srgb, 1.0);
}

float4 PS_Vignette(VSQOut IN) : SV_TARGET
{
    float3 srgb = gAlbedoTex.Sample(samLinear, IN.uv).rgb;

    float2 aspect = float2(InvResolution.y / InvResolution.x, 1.0);
    float2 d = (IN.uv - VignetteCenter) * aspect;
    float vig = 1.0 - VignetteStrength * pow(saturate(length(d) * 1.41421356), VignettePower);

    return float4(srgb * vig, 1.0);
}

float4 PS_Invert(VSQOut IN) : SV_TARGET
{
    float3 c = gAlbedoTex.Sample(samLinear, IN.uv).rgb;
    return float4(1.0 - c, 1.0);
}

float4 PS_Grayscale(VSQOut IN) : SV_TARGET
{
    float3 c = gAlbedoTex.Sample(samLinear, IN.uv).rgb;
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    return float4(l.xxx, 1.0);
}

float4 PS_Pixelate(VSQOut IN) : SV_TARGET
{
    float2 block = max(PixelateSize, 1.0) * InvResolution;
    float2 uv0 = (floor(IN.uv / block) + 0.5) * block;
    float3 c = gAlbedoTex.Sample(samLinear, uv0).rgb;
    return float4(c, 1.0);
}

float4 PS_Posterize(VSQOut IN) : SV_TARGET
{
    float levels = max(PosterizeLevels, 2.0);
    float3 c = gAlbedoTex.Sample(samLinear, IN.uv).rgb;
    c = floor(c * levels) / levels;
    return float4(c, 1.0);
}

float4 PS_Saturation(VSQOut IN) : SV_TARGET
{
    float3 c = gAlbedoTex.Sample(samLinear, IN.uv).rgb;
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(l.xxx, c, Saturation);
    return float4(saturate(c), 1.0);
}