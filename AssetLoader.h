#pragma once
#include "Meshes.h"
#include <string>
#include "Material.h"
#include <ResourceUploadBatch.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class AssetLoader
{
public:
	Mesh LoadGeometry(const std::string& objPath);
	Material LoadMaterial(const std::string& mtlFile, const std::string& materialName);
	void LoadTexture(ID3D12Device* device, ResourceUploadBatch& uploadBatch, DX12Framework* framework, const wchar_t* filename);

private:
	std::vector <ComPtr<ID3D12Resource>> textures;
	std::vector <ComPtr<ID3D12Resource>> texturesUploadHeaps;
};

