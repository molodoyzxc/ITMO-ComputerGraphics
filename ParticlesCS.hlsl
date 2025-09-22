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
    p.lifetime = 20.0;
    p.size = 1.0f;

    gOut.Append(p);
}

[numthreads(256, 1, 1)]
void CS_Update(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= aliveCount)
        return;

    Particle p = gIn.Consume();
    p.vel += accel * dt;
    p.pos += p.vel * dt;
    p.age += dt;

    if (p.age < p.lifetime)
        gOut.Append(p);
}
