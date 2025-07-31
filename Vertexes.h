#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct Vertex {
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 uv;
	XMFLOAT3 tangent = { 0.0f, 0.0f, 0.0f};
	float handedness = 0;
};