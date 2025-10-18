Texture2D gInput : register(t0);
Texture2D gHistory : register(t1);
Texture2D gDepthPrev : register(t2);
Texture2D gDepthCurr : register(t3);
Texture2D<float2> gVelocity : register(t4);
SamplerState gSamp : register(s0);

cbuffer TAAParams : register(b0)
{
    float2 jitterCur;
    float2 jitterPrev;
    float alphaNew;
    float _pad0;
    float2 invResolution;
    float clampK;
    float reactiveK;
    float4x4 CurrViewProjInv;
    float4x4 PrevViewProj;
    float zDiffNdc;
    float uvGuard;
    float2 _pad1;
}

static const float3 LUMA = float3(0.299, 0.587, 0.114);

float4 AdjustHDR_InverseLuma(float3 c)
{
    float Y = dot(c, LUMA);
    float w = 1.0 / (1.0 + max(Y, 1e-6));
    return float4(c * w, w);
}

float4 AdjustHDR_Log(float3 c)
{
    float3 x = max(c, 1e-6);
    return float4(log(x), 1.0);
}

float3 RGB_to_YCoCg(float3 c)
{
    float Y = dot(c, float3(0.25, 0.5, 0.25));
    float Co = dot(c, float3(0.5, 0.0, -0.5));
    float Cg = dot(c, float3(-0.25, 0.5, -0.25));
    return float3(Y, Co, Cg);
}

float3 YCoCg_to_RGB(float3 ycc)
{
    float Y = ycc.x, Co = ycc.y, Cg = ycc.z;
    float3 c;
    c.r = Y + Co - Cg;
    c.g = Y + Cg;
    c.b = Y - Co - Cg;
    return c;
}

float3 BoxMean3x3(Texture2D tex, float2 uv, float2 invRes)
{
    float3 acc = 0;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            acc += tex.Sample(gSamp, uv + float2(dx, dy) * invRes).rgb;
        }
    }
    return acc / 9.0;
}

float Luma(float3 c)
{
    return dot(c, LUMA);
}

float3 SampleHistoryCatmullRom(Texture2D tex, float2 uv)
{
    float2 texSize;
    tex.GetDimensions(texSize.x, texSize.y);
    float2 pos = uv * texSize - 0.5;
    float2 f = frac(pos);
    int2 p = int2(floor(pos));
    float3 s[4][4];
    [unroll]
    for (int j = -1; j <= 2; ++j)
    {
        [unroll]
        for (int i = -1; i <= 2; ++i)
        {
            s[j + 1][i + 1] = tex.SampleLevel(gSamp, (float2(p + int2(i, j)) + 0.5) / texSize, 0).rgb;
        }
    }
   
    float3 cX[4];
    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        float3 a0 = s[j][1];
        float3 a1 = 0.5 * (s[j][2] - s[j][0]);
        float3 a2 = s[j][0] - 2.5 * s[j][1] + 2.0 * s[j][2] - 0.5 * s[j][3];
        float3 a3 = 0.5 * (s[j][3] - s[j][0]) + 1.5 * (s[j][1] - s[j][2]);
        cX[j] = ((a3 * f.x + a2) * f.x + a1) * f.x + a0;
    }
   
    float3 a0 = cX[1];
    float3 a1 = 0.5 * (cX[2] - cX[0]);
    float3 a2 = cX[0] - 2.5 * cX[1] + 2.0 * cX[2] - 0.5 * cX[3];
    float3 a3 = 0.5 * (cX[3] - cX[0]) + 1.5 * (cX[1] - cX[2]);
    float3 r = ((a3 * f.y + a2) * f.y + a1) * f.y + a0;
    return r;
}

float4 PS_TAA(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float2 ndcXY = uv * 2.0 - 1.0;
    float zCur = gDepthCurr.SampleLevel(gSamp, uv, 0).r;
    float4 currClip = float4(ndcXY, zCur, 1.0);
    float4 worldPos = mul(CurrViewProjInv, currClip);
    worldPos /= max(worldPos.w, 1e-6);
   
    float2 vel = gVelocity.SampleLevel(gSamp, uv, 0);
    float2 prevUV = uv + vel;
    float4 prevClip = mul(PrevViewProj, float4(worldPos.xyz, 1.0));
    float rw = max(prevClip.w, 1e-6);
    float prevZndc = prevClip.z / rw;
   
    float2 guard = uvGuard * invResolution;
    bool outOfBounds = any(prevUV < (float2) 0.0 - guard) || any(prevUV > (float2) 1.0 + guard);
    prevUV = clamp(prevUV, guard, 1.0 - guard);
   
    float zPrev = gDepthPrev.SampleLevel(gSamp, prevUV, 0).r;
    float dz = abs(zPrev - prevZndc);
    bool disocc = outOfBounds || (abs(rw) < 1e-4) || (dz > zDiffNdc);
   
    float3 curr = gInput.Sample(gSamp, uv).rgb;
   
    float3 meanN = BoxMean3x3(gInput, uv, invResolution);
    float3 varAcc = 0;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            float3 s = gInput.Sample(gSamp, uv + float2(dx, dy) * invResolution).rgb;
            varAcc += (s - meanN) * (s - meanN);
        }
    }
    float3 variance = varAcc / 9.0;
    float3 hist = gHistory.Sample(gSamp, prevUV).rgb;
   
    float lc = Luma(curr);
    float lmn = Luma(meanN);
    float cgrad = saturate(abs(lc - lmn) * 4.0);
    float lh = Luma(hist);
    float diff = abs(lc - lh);
   
    float vlen = length(vel) * max(invResolution.x, invResolution.y);
    float velBoost = saturate(vlen * 400.0);
   
    float3 nmin = curr, nmax = curr;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            float3 s = gInput.Sample(gSamp, uv + float2(dx, dy) * invResolution).rgb;
            nmin = min(nmin, s);
            nmax = max(nmax, s);
        }
    }
    float3 center = 0.5 * (nmin + nmax);
    float3 halfW = 0.5 * (nmax - nmin);
   
    halfW = max(halfW - clampK, 0.0) + sqrt(max(variance, 0.0)) * 0.25;
    float3 lo = center - halfW;
    float3 hi = center + halfW;
   
    float3 histYCC = RGB_to_YCoCg(hist);
    float3 loYCC = RGB_to_YCoCg(lo);
    float3 hiYCC = RGB_to_YCoCg(hi);
    histYCC = clamp(histYCC, loYCC, hiYCC);
    hist = YCoCg_to_RGB(histYCC);
   
    float3 currStable = lerp(curr, meanN, 0.3);
   
    float a = alphaNew + reactiveK * (diff + 0.5 * cgrad + 0.5 * velBoost);
    a = saturate(a);
   
    a = disocc ? 1.0 : a;
    
    float3 log_curr = log(max(currStable, 1e-6.xxx));
    float3 log_hist = log(max(hist, 1e-6.xxx));
    float3 log_out = lerp(log_hist, log_curr, a);
    float3 outc = exp(log_out);
    
    float4 currAdj = AdjustHDR_InverseLuma(currStable);
    float4 histAdj = AdjustHDR_InverseLuma(hist);
    float currentWeight = a * currAdj.a;
    float previousWeight = (1.0 - a) * histAdj.a;
    //outc = (currAdj.rgb * currentWeight + histAdj.rgb * previousWeight) / max(currentWeight + previousWeight, 1e-6);
    
    return float4(outc, 1.0);
}