Texture2D<float> gDepthCurr : register(t0);
SamplerState gSamp : register(s0);

cbuffer VelCB : register(b1)
{
    float2 invResolution;
    float2 jitterCur;
    float2 jitterPrev;
    float4x4 CurrViewProjInv;
    float4x4 PrevViewProj;
    float uvGuard;
    float zDiffNdc;
    float2 _pad;
}

struct VSQOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSQOut VS_Quad(uint id : SV_VertexID)
{
    VSQOut o;
    float2 v[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    o.pos = float4(v[id], 0, 1);
    o.uv = v[id] * float2(0.5, -0.5) + 0.5;
    return o;
}

float2 PackVel(float2 vel)
{
    return vel;
}

float4 PS_Velocity(VSQOut IN) : SV_Target
{
    float2 uv = IN.uv;
    
    float depth = gDepthCurr.SampleLevel(gSamp, uv, 0);
    if (depth >= 1.0)
        return float4(0, 0, 0, 0);
    
    float2 ndc = float2(2.0 * uv.x - 1.0, 2.0 * uv.y - 1.0);
    float4 currClip = float4(ndc.x, ndc.y, depth, 1.0);
    float4 worldPos = mul(CurrViewProjInv, currClip);
    worldPos /= max(worldPos.w, 1e-6);
    
    float4 prevClip = mul(PrevViewProj, float4(worldPos.xyz, 1.0));
    float rw = max(prevClip.w, 1e-6);
    float2 prevUV = prevClip.xy / rw * 0.5 + 0.5;
    
    float2 curJ = uv - jitterCur * invResolution;
    float2 prevJ = prevUV - jitterPrev * invResolution;
    
    float2 vel = prevJ - curJ;
    
    float2 guard = uvGuard * invResolution;
    bool oob = any(prevUV < (float2) 0.0 - guard) || any(prevUV > (float2) 1.0 + guard);
    if (oob)
        vel = 0.0;

    return float4(vel, 0, 0);
}

