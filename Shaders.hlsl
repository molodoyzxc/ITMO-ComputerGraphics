// ===========================================
// Шейдеры для простого куба с освещением Фонга
// ===========================================

// Единственный константный буфер в регистре b0
cbuffer CB : register(b0)
{
    float4x4 WVP; // World-View-Projection
    float4 LightDir; // Направление света (как вектор-луч от источника)
    float4 LightColor; // Цвет/интенсивность источника
    float4 AmbientColor; // Цвет фонового освещения
    float4 EyePos; // Позиция камеры в мировых координатах
    float4 ObjectColor; // Цвет объекта (RGBA)
};

// Входная структура вершинного шейдера
struct VSInput
{
    float3 pos : POSITION; // Локальная позиция вершины
    float3 normal : NORMAL; // Нормаль вершины (в модельном/мировом пространстве)
};

// Выход вершинного шейдера и вход пиксельного
struct VSOutput
{
    float4 posH : SV_POSITION; // Клип-пространственная позиция
    float3 normal : NORMAL; // Нормаль для пиксельного шейдера
    float3 worldPos : TEXCOORD0; // Мировая позиция (для расчёта V)
};

// Вершинный шейдер
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    // Позиция в экранных (clip) координатах
    output.posH = mul(WVP, float4(input.pos, 1.0f));

    // Передаём нормаль (в упрощённой модели считаем, что нормаль уже в нужном пространстве)
    output.normal = input.normal;

    // Передаём мировую позицию (если у модели нет отдельной матрицы World, просто input.pos)
    output.worldPos = input.pos;

    return output;
}

// Пиксельный шейдер с освещением Фонга
float4 PSMain(VSOutput input) : SV_TARGET
{
    // 1) Нормализуем нормаль
    float3 N = normalize(input.normal);

    // 2) Направление от поверхности к источнику
    float3 L = normalize(-LightDir.xyz);

    // 3) Ламбертовское (Diffuse) освещение
    float ndotl = saturate(dot(N, L));
    float3 diffuse = LightColor.rgb * ObjectColor.rgb * ndotl;

    // 4) Фоновое (Ambient) освещение
    float3 ambient = AmbientColor.rgb * ObjectColor.rgb;

    // 5) Спекулярная компонента (Phong)
    float3 V = normalize(EyePos.xyz - input.worldPos);
    float3 R = reflect(-L, N);
    float cosPhi = saturate(dot(V, R));
    float specIntensity = pow(cosPhi, 32.0f);
    float3 specular = LightColor.rgb * specIntensity;

    // 6) Суммарный цвет и возврат
    float3 resultColor = ambient + diffuse + specular;
    return float4(resultColor, 1.0f);
}
