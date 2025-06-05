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

    for (int stack = 0; stack <= stacks; ++stack)
    {
        float phi = XM_PI * stack / stacks; // от 0 до PI

        for (int slice = 0; slice <= slices; ++slice)
        {
            float theta = 2 * XM_PI * slice / slices; // от 0 до 2PI

            float x = sinf(phi) * cosf(theta);
            float y = cosf(phi);
            float z = sinf(phi) * sinf(theta);

            XMFLOAT3 pos = { x * radius, y * radius, z * radius };
            XMFLOAT3 normal = { x, y, z };

            m.vertices.push_back({ pos, normal });
        }
    }

    for (int stack = 0; stack < stacks; ++stack)
    {
        for (int slice = 0; slice < slices; ++slice)
        {
            int a = stack * (slices + 1) + slice;
            int b = (stack + 1) * (slices + 1) + slice;

            m.indices.push_back(a);
            m.indices.push_back(b);
            m.indices.push_back(a + 1);

            m.indices.push_back(a + 1);
            m.indices.push_back(b);
            m.indices.push_back(b + 1);
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

Mesh CreateRomanDigitMesh(char symbol) {
    switch (symbol) {
    case '0': return CreateZero();
    case 'I': return CreateRomanI();
    case 'V': return CreateRomanV();
    case 'X': return CreateRomanX();
    default: throw std::runtime_error("Unknown Roman digit");
    }
}

Mesh CreateZero() {
    Mesh m;
    m.vertices = {
        {{-0.1, -0.3, 0,},{0, 0, -1,},},
        {{-0.3, -0.5, 0,},{0, 0, -1,},},
        {{-0.3, 0.5, 0,},{0, 0, -1,},},
        {{-0.1, 0.3, 0,},{0, 0, -1,},},

        {{0.1, 0.3, 0,},{0, 0, -1,},},
        {{0.3, 0.5, 0,},{0, 0, -1,},},
        {{0.3, -0.5, 0,},{0, 0, -1,},},
        {{0.1, -0.3, 0,},{0, 0, -1,},},
    };
    m.indices = {
        0, 1, 2,
        0, 2, 3,

        4, 5, 6,
        4, 6, 7,

        3, 2, 5,
        3, 5, 4,

        0, 7, 6,
        0, 6, 1,
    };
    return m;
}

Mesh CreateRomanI() {
    Mesh m;
    float w = 0.1f, h = 0.5f;
    m.vertices = {
        {{-w, -h, 0}, {0, 0, -1},},
        {{-w,  h, 0}, {0, 0, -1},},
        {{ w,  h, 0}, {0, 0, -1},},
        {{ w, -h, 0}, {0, 0, -1},},
    };
    m.indices = {
        0,1,2,
        0,2,3
    };
    return m;
}

Mesh CreateRomanV() {
    Mesh m;
    m.vertices = {
        {{-0.1, -0.5, 0,}, {0, 0, -1,},},
        {{-0.3, 0.5, 0,}, {0, 0, -1,},},
        {{-0.1, 0.5, 0,}, {0, 0, -1,},},
        {{0, 0, 0,}, {0, 0, -1,},},
        {{0.1, 0.5, 0,}, {0, 0, -1,},},
        {{0.3, 0.5, 0,}, {0, 0, -1,},},
        {{0.1, -0.5, 0,}, {0, 0, -1,},},
    };
    m.indices = {
        0, 1, 2,
        0, 2, 3,
        0, 3, 6,
        3, 4, 5,
        3, 5, 6,
    };
    return m;
};

Mesh CreateRomanX() {
    Mesh m;
    m.vertices = {
        // upper
        {{-0.1, 0, 0},{0, 0, -1,},},
        {{-0.3, 0.5, 0},{0, 0, -1,},},
        {{-0.1, 0.5, 0},{0, 0, -1,},},
        {{0, 0.2, 0},{0, 0, -1,},},
        {{0.1, 0.5, 0},{0, 0, -1,},},
        {{0.3, 0.5, 0},{0, 0, -1,},},
        {{0.1, 0, 0},{0, 0, -1,},},

        // lower
        {{0.3, -0.5, 0},{0, 0, -1,},},
        {{0.1, -0.5, 0},{0, 0, -1,},},
        {{0, -0.2, 0},{0, 0, -1,},},
        {{-0.1, -0.5, 0},{0, 0, -1,},},
        {{-0.3, -0.5, 0},{0, 0, -1,},},
    };
    m.indices = {
        // upper
        0, 1, 2,
        0, 2, 3,
        0, 3, 6,
        3, 4, 5,
        3, 5, 6,

        // lower
        6, 7, 8,
        6, 8, 9,
        6, 9, 0,
        9, 10, 11,
        9, 11, 0,
    };
    return m;
};

std::vector<Mesh> GenerateRomanDigits() {
    std::vector<std::string> digits = {
        "0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"
    };

    std::vector<Mesh> result;

    for (const std::string& digit : digits) {
        Mesh combined;
        float offset = 0;
        for (char c : digit) {
            Mesh part = CreateRomanDigitMesh(c);

            // сместим символ по X
            for (auto& v : part.vertices) {
                v.Pos.x += offset;
            }

            offset += 0.4f; // между символами

            // объединение в один меш
            UINT16 base = static_cast<UINT16>(combined.vertices.size());
            combined.vertices.insert(combined.vertices.end(), part.vertices.begin(), part.vertices.end());
            for (UINT16 idx : part.indices)
                combined.indices.push_back(base + idx);
        }

        result.push_back(combined);
    }

    return result;
}
