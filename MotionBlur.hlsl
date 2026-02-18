cbuffer MotionBlurCB : register(b1)
{
    row_major float4x4 ViewProj;
    row_major float4x4 PrevViewProj;
    row_major float4x4 InvViewProj;
    float2 ScreenSize;
    float MotionBlurStrength;
    float MotionBlurMaxPixels;
    uint MotionBlurSamples;
    float _padMB;
};

Texture2D<float4> gColorTex : register(t20);
Texture2D<float> gDepthTex : register(t0);
SamplerState samLinear : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float2 NdcFromUv(float2 uv)
{
    return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

float3 ReconstructWorld(float2 uv, float depth)
{
    float2 ndc = NdcFromUv(uv);
    float4 clip = float4(ndc, depth, 1.0);
    float4 world = mul(clip, InvViewProj);
    return world.xyz / max(world.w, 1e-6);
}

float2 ProjectToNdc(float3 worldPos, float4x4 m)
{
    float4 clip = mul(float4(worldPos, 1.0), m);
    if (clip.w < 1e-5)
        return 0.0.xx;
    return clip.xy / clip.w;
}

float4 PS_MotionBlur(VSOut IN) : SV_TARGET
{
    float2 uv = IN.uv;
    int2 pix = int2(uv * ScreenSize.xy);
    pix = clamp(pix, int2(0, 0), int2((int) ScreenSize.x - 1, (int) ScreenSize.y - 1));

    float depth = gDepthTex.Load(int3(pix, 0));
    if (depth >= 1.0)
        return gColorTex.Load(int3(pix, 0));

    float2 uvCenter = (float2(pix) + 0.5) / ScreenSize.xy;
    float3 worldPos = ReconstructWorld(uvCenter, depth);

    float2 currNdc = ProjectToNdc(worldPos, ViewProj);
    float2 prevNdc = ProjectToNdc(worldPos, PrevViewProj);
    float2 velNdc = (currNdc - prevNdc) * MotionBlurStrength;
    
    float2 velUv = float2(velNdc.x * 0.5, -velNdc.y * 0.5);

    float lenPx = length(velUv * ScreenSize.xy);
    if (lenPx < 0.01f) 
        return gColorTex.Load(int3(pix, 0));

    float maxPx = max(MotionBlurMaxPixels, 1.0);
    float scale = min(1.0, maxPx / lenPx) * 2.2f;
    
    velUv *= scale;

    uint samples = max(MotionBlurSamples, 6u);

    float4 sum = 0.0;
    float wsum = 0.0;

    for (uint i = 0; i < samples; ++i)
    {
        float t = (float) i / (float) (samples - 1);
        float a = t * 2.0 - 1.0;
        float2 suv = saturate(uvCenter + velUv * a);

        float4 c = gColorTex.SampleLevel(samLinear, suv, 0);
        float w = 1.0 - abs(a);
        sum += c * w;
        wsum += w;
    }
    
    return sum / max(wsum, 1e-6);
}