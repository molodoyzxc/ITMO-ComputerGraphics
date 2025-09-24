Texture2D gDepthTex : register(t2);

cbuffer ParticleSceneCB : register(b6)
{
    row_major float4x4 ViewProj;
    row_major float4x4 InvViewProj;
    float2 ScreenSize;
    float CollisionEps;
    float _padPSCB;
}

int2 UVtoTexel(float2 uv, float2 screenSize)
{
    float2 maxUV = 1.0 - (1.0 / screenSize);
    float2 uvc = clamp(uv, 0.0.xx, maxUV);
    return int2(uvc * screenSize);
}

float SampleSceneDepthMin3x3(float2 uv)
{
    float2 texelSize = 1.0 / ScreenSize;
    float dmin = 1.0;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            float2 uv2 = clamp(uv + float2(dx, dy) * texelSize, 0.0.xx, 1.0.xx - texelSize);
            int2 ij = UVtoTexel(uv2, ScreenSize);
            float s = gDepthTex.Load(int3(ij, 0)).x;
            dmin = min(dmin, s);
        }
    }
    return dmin;
}

bool ProjectToUVDepth(float3 wp, out float2 uv, out float depth01)
{
    float4 clip = mul(float4(wp, 1.0), ViewProj);
    if (clip.w <= 0.0)
    {
        uv = float2(0.0, 0.0);
        depth01 = 0.0;
        return false;
    }
    float3 ndc = clip.xyz / clip.w;
    uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    depth01 = ndc.z;
    
    if (depth01 < 0.0 || depth01 > 1.0)
    {
        uv = float2(0.0, 0.0);
        depth01 = 0.0;
        return false;
    }
    
    return all(uv >= 0.0) && all(uv < 1.0);
}

float LinearDepth(float ndcZ)
{
    const float near = 0.1;
    const float far = 5000.0;
    return (near * far) / (far + ndcZ * (near - far));
}

float3 ClampToDepthContact(float3 p0, float3 p1, uint iters)
{
    float3 a = p0, b = p1;
    [unroll]
    for (uint i = 0; i < iters; i++)
    {
        float3 m = 0.5 * (a + b);
        float2 uv;
        float d01;
        if (!ProjectToUVDepth(m, uv, d01))
        {
            b = m;
            continue;
        }
        float scene = SampleSceneDepthMin3x3(uv);
        if (scene >= 1.0 - 1e-5)
        {
            a = m;
            continue;
        }
        float particleLinear = LinearDepth(d01);
        float sceneLinear = LinearDepth(scene);
        if (particleLinear > sceneLinear - CollisionEps)
            b = m;
        else
            a = m;
    }
    return a;
}

cbuffer ParticleUpdateCB : register(b5)
{
    float dt;
    float3 accel;
    uint spawnCount;
    float3 emitterPos;
    float initialSpeed;
    uint aliveCount;
};

struct Particle
{
    float3 pos;
    float3 vel;
    float age;
    float lifetime;
    float size;
};

ConsumeStructuredBuffer<Particle> gIn : register(u0);
AppendStructuredBuffer<Particle> gOut : register(u1);

uint WangHash(uint s)
{
    s = (s ^ 61u) ^ (s >> 16);
    s *= 9u;
    s ^= s >> 4;
    s *= 0x27d4eb2d;
    s ^= s >> 15;
    return s;
}

float rand01(uint s)
{
    return (WangHash(s) & 0x00FFFFFF) / 16777216.0;
}

[numthreads(256, 1, 1)]
void CS_Emit(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= spawnCount)
        return;
    uint seed = 1337u + i * 747796405u;
    Particle p;
    p.pos = emitterPos;
    float3 dir = normalize(float3(
        rand01(seed) * 2 - 1,
        rand01(seed + 1) * 2 - 1,
        rand01(seed + 2) * 2 - 1));
    p.vel = dir * initialSpeed;
    p.age = 0.0;
    p.lifetime = 50.0;
    p.size = 1.0f;
    gOut.Append(p);
}

[numthreads(256, 1, 1)]
void CS_Update(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= aliveCount)
        return;
    Particle p = gIn.Consume();
    float3 oldPos = p.pos;
    
    p.vel += accel * dt;
    float3 newPos = p.pos + p.vel * dt;
    
    float2 uv;
    float depth01;
    bool ok = ProjectToUVDepth(newPos, uv, depth01);
    if (ok)
    {
        float scene = SampleSceneDepthMin3x3(uv);
        
        if (scene < 1.0 - 1e-5)
        {
            float particleLinear = LinearDepth(depth01);
            float sceneLinear = LinearDepth(scene);
            if (particleLinear > sceneLinear - CollisionEps)
            {
                newPos = ClampToDepthContact(oldPos, newPos, 10);
                p.vel = 0.0.xxx;
            }
        }
    }
    p.pos = newPos;
    p.age += dt;
    if (p.age < p.lifetime)
        gOut.Append(p);
}