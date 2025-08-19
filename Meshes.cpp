#include "Meshes.h"
#include <string>
#include <stdexcept>
using namespace DirectX;

Mesh CreateCube() {
    Mesh m;
    m.vertices = {
    // ѕередна€€ (0-3)
    {{-0.5, -0.5, -0.5}, {0,0,-1}, {0, 1}},
    {{-0.5, 0.5, -0.5}, {0,0,-1}, {0, 0}},
    {{0.5, 0.5, -0.5}, {0,0,-1}, {1, 0}},
    {{0.5, -0.5, -0.5}, {0,0,-1}, {1, 1}},

    // «адн€€ (4Ц7)
    {{-0.5, -0.5, 0.5}, {0,0,1}, {0, 0}},
    {{-0.5, 0.5, 0.5}, {0,0,1}, {0, 0}},
    {{0.5, 0.5, 0.5}, {0,0,1}, {0, 0}},
    {{0.5, -0.5, 0.5}, {0,0,1}, {0, 0}},

    // Ћева€ (8Ц11)
    {{-0.5, -0.5, -0.5}, {-1,0,0}, {0, 0}},
    {{-0.5, 0.5, -0.5}, {-1,0,0}, {0, 0}},
    {{-0.5, 0.5, 0.5}, {-1,0,0}, {0, 0}},
    {{-0.5, -0.5, 0.5}, {-1,0,0}, {0, 0}},

    // ѕрава€ (12Ц15)
    {{0.5, -0.5, -0.5}, {1,0,0}, {0, 0}},
    {{0.5, 0.5, -0.5}, {1,0,0}, {0, 0}},
    {{0.5, 0.5, 0.5}, {1,0,0}, {0, 0}},
    {{0.5, -0.5, 0.5}, {1,0,0}, {0, 0}},

    // ¬ерхн€€ (16Ц19)
    {{-0.5, 0.5, -0.5}, {0,1,0}, {0, 0}},
    {{-0.5, 0.5, 0.5}, {0,1,0}, {0, 0}},
    {{0.5, 0.5, 0.5}, {0,1,0}, {0, 0}},
    {{0.5, 0.5, -0.5}, {0,1,0}, {0, 0}},

    // Ќижн€€ (20Ц23)
    {{-0.5, -0.5, -0.5}, {0,-1,0}, {0, 0}},
    {{-0.5, -0.5, 0.5}, {0,-1,0}, {0, 0}},
    {{0.5, -0.5, 0.5}, {0,-1,0}, {0, 0}},
    {{0.5, -0.5, -0.5}, {0,-1,0}, {0, 0}},
    };
    m.indices = {
        // ѕередн€€ (0-3)
        0, 1, 2,
        0, 2, 3,

        // «адн€€ (4Ц7)
        4, 7, 6,
        4, 6, 5,

        // Ћева€ (8Ц11)
        11, 10, 9,
        11, 9, 8,

        // ѕрава€ (12Ц15)
        12, 13, 14,
        12, 14, 15,

        // ¬ерхн€€ (16Ц19)
        16, 17, 18,
        16, 18, 19,

        // Ќижн€€ (20Ц23)
        21, 20, 23,
        21, 23, 22,
    };
    return m;
}

Mesh CreateSphere(int slices, int stacks, float radius)
{
    Mesh m;

    // ¬ертикальное направление: от -?/2 (юг) до +?/2 (север)
    for (int stack = 0; stack <= stacks; ++stack)
    {
        float v = static_cast<float>(stack) / stacks;
        float phi = XM_PI * (v - 0.5f); // от -?/2 до +?/2

        float y = sinf(phi);
        float r = cosf(phi); // горизонтальный радиус кольца

        for (int slice = 0; slice <= slices; ++slice)
        {
            float u = static_cast<float>(slice) / slices;
            float theta = XM_2PI * u; // от 0 до 2?

            float x = r * cosf(theta);
            float z = r * sinf(theta);

            XMFLOAT3 pos = { x * radius, y * radius, z * radius };
            XMFLOAT3 normal = { x, y, z };

            m.vertices.push_back({ pos, normal });
        }
    }

    // »ндексы (двойной треугольник на каждую "€чейку")
    for (int stack = 0; stack < stacks; ++stack)
    {
        for (int slice = 0; slice < slices; ++slice)
        {
            int row1 = stack * (slices + 1);
            int row2 = (stack + 1) * (slices + 1);

            int a = row1 + slice;
            int b = row2 + slice;
            int c = row1 + slice + 1;
            int d = row2 + slice + 1;

            // ѕор€док дл€ лицевых треугольников (по часовой при LH-системе)
            m.indices.push_back(a);
            m.indices.push_back(b);
            m.indices.push_back(c);

            m.indices.push_back(c);
            m.indices.push_back(b);
            m.indices.push_back(d);
        }
    }

    return m;
}

Mesh CreateTestTriangle() {
    Mesh m;
    m.vertices = {
        {{-1,-1,0,},{0,0,-1}, {0, 1}},
        {{-1,1,0,},{0,0,-1}, {0, 0}},
        {{1,1,0,},{0,0,-1}, {1, 0}},
    };
    m.indices = {
        0, 1, 2,
    };
    return m;
}

Mesh CreatePlane() {
    Mesh m;
    m.vertices = {
        {{-0.5, -0.5, 0}, {0, 0, -1}, {0, 1}},
        {{-0.5, 0.5, 0}, {0, 0, -1}, {0, 0}},
        {{0.5, 0.5, 0}, {0, 0, -1}, {1, 0}},
        {{0.5, -0.5, 0}, {0, 0, -1}, {1, 1}},
    };
    
    m.indices = {
        0, 1, 2,
        0, 2, 3,
    };

    return m;
}
