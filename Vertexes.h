#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct Vertex {
	XMFLOAT3 Pos, Normal;
	XMFLOAT2 uv;
};