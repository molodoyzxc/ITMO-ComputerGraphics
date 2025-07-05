#pragma once
#include <vector>
#include <basetsd.h>
#include <DirectXMath.h>
#include "Vertexes.h"

using namespace DirectX;


struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<UINT32> indices;
};

Mesh CreateCube();
Mesh CreateSphere(int slices = 20, int stacks = 20, float radius = 1.0f);
Mesh CreateTestTriangle();
Mesh CreatePlane();
Mesh CreateZero();
Mesh CreateRomanI();
Mesh CreateRomanV();
Mesh CreateRomanX();
std::vector<Mesh> GenerateRomanDigits();