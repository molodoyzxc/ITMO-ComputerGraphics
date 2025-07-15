cbuffer CB : register(b0)
{
    float4x4 World;
    float4x4 WVP;
    float4 LightDir;
    float4 LightColor;
    float4 AmbientColor;
    float4 EyePos;
    
    float4 ObjectColor;
    float2 uvScale;
    float2 uvOffset;
    
    // materials
    float4 Ka; // ambient
    float4 Kd; // diffuse
    float4 Ks; // specular
    float Ns; // shininess
    float3 pad; 
};

Texture2D diffuseTex : register(t0);
SamplerState samp : register(s0);

struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0; 
};

struct VSOutput
{
    float4 posH : SV_POSITION;
    float3 normal : NORMAL;
    float3 worldPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    
    output.posH = mul(WVP, float4(input.pos, 1.0f));
    
    float3x3 world3x3 = (float3x3) World;
    output.normal = normalize(mul(world3x3, input.normal));
    
    output.worldPos = mul(World, float4(input.pos, 1.0f)).xyz;
    
    output.uv = input.uv;

    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    
    float3 L = normalize(-LightDir.xyz);
    
    float ndotl = saturate(dot(N, L));
    
    float3 V = normalize(EyePos.xyz - input.worldPos);
    float3 R = reflect(-L, N);
    float cosPhi = saturate(dot(V, R));
    
    float specIntensity = pow(cosPhi, Ns);
    float3 specular = LightColor.rgb * Ks.rgb * specIntensity;
    
    float2 uvTex = input.uv * uvScale + uvOffset;
    float4 texColor = diffuseTex.Sample(samp, uvTex);
    clip(texColor.a - 0.1);
    
    float3 baseColor;
    baseColor = ObjectColor.rgb * texColor.rgb;
    
    float3 diffuse = LightColor.rgb * Kd.rgb * baseColor * ndotl;
    float3 ambient = AmbientColor.rgb * Ka.rgb * baseColor;
    
    float3 resultColor = ambient + diffuse + specular;
    
    return float4(resultColor, ObjectColor.a);
}

