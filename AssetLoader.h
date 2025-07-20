#pragma once
#include "Meshes.h"
#include <string>
#include "Material.h"
#include <ResourceUploadBatch.h>
#include <wrl.h>
#include "SceneObject.h"
#include <vector>

using Microsoft::WRL::ComPtr;

class AssetLoader
{
public:
	Mesh LoadGeometry(const std::string& objPath);
	Material LoadMaterial(const std::string& mtlFile, const std::string& materialName);
	void LoadTexture(ID3D12Device* device, ResourceUploadBatch& uploadBatch, DX12Framework* framework, const wchar_t* filename);
	std::vector<SceneObject> LoadSceneObjects(const std::string& objPath);

private:
	std::vector <ComPtr<ID3D12Resource>> textures;
};

