#include "AssetLoader.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include <unordered_map>
#include <WICTextureLoader.h>
#include "DX12Framework.h"

Mesh AssetLoader::LoadGeometry(const std::string& objPath)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, nullptr, &warn, &err, objPath.c_str(), nullptr);

    Mesh mesh;

    std::unordered_map<uint64_t, UINT32> uniqueVerts;
    uniqueVerts.reserve(shapes.size() * 3);

    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            uint64_t key = (uint64_t(idx.vertex_index + 1) << 42)
                | (uint64_t(idx.texcoord_index + 1) << 21)
                | uint64_t(idx.normal_index + 1);

            auto it = uniqueVerts.find(key);
            if (it == uniqueVerts.end()) {
                Vertex v{};
                v.Pos = {
                  attrib.vertices[3 * idx.vertex_index + 0],
                  attrib.vertices[3 * idx.vertex_index + 1],
                  attrib.vertices[3 * idx.vertex_index + 2]
                };
                if (idx.normal_index >= 0) {
                    v.Normal = {
                      attrib.normals[3 * idx.normal_index + 0],
                      attrib.normals[3 * idx.normal_index + 1],
                      attrib.normals[3 * idx.normal_index + 2]
                    };
                }
                if (idx.texcoord_index >= 0) {
                    v.uv = {
                      attrib.texcoords[2 * idx.texcoord_index + 0],
                      1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]
                    };
                }
                UINT32 newIndex = static_cast<UINT32>(mesh.vertices.size());
                mesh.vertices.push_back(v);
                mesh.indices.push_back(newIndex);
                uniqueVerts[key] = newIndex;
            }
            else {
                mesh.indices.push_back(it->second);
            }
        }
    }

    return mesh;
}

Material AssetLoader::LoadMaterial(const std::string& mtlFile, const std::string& materialName)
{
    std::ifstream in(mtlFile);
    if (!in.is_open()) throw std::runtime_error("не удалось открыть " + mtlFile);

    Material mat;
    std::string token, currentName;
    bool inTarget = false;

    while (in >> token) {
        if (token == "newmtl") {
            in >> currentName;
            inTarget = (currentName == materialName);
        }
        else if (inTarget) {
            if (token == "Ka") {
                in >> mat.ambient.x >> mat.ambient.y >> mat.ambient.z;
            }
            else if (token == "Kd") {
                in >> mat.diffuse.x >> mat.diffuse.y >> mat.diffuse.z;
            }
            else if (token == "Ks") {
                in >> mat.specular.x >> mat.specular.y >> mat.specular.z;
            }
            else if (token == "Ns") {
                in >> mat.shininess;
            }
            else if (token == "map_Kd") {
                in >> mat.diffuseTexPath;
            }
        }
    }
    return mat;
}

void AssetLoader::LoadTexture(ID3D12Device* device, ResourceUploadBatch& uploadBatch, DX12Framework* framework, const wchar_t* filename)
{
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> textureUploadHeap;

    HRESULT hr = DirectX::CreateWICTextureFromFile(
        device,
        uploadBatch,
        filename,
        texture.ReleaseAndGetAddressOf(),
        textureUploadHeap.ReleaseAndGetAddressOf()
    );
    if (FAILED(hr)) {
        throw std::runtime_error("Create texture from file failed");
    }

    auto desc = texture->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(-1);

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(
        framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
        framework->AllocateSrvDescriptor(),
        framework->GetSrvDescriptorSize()
    );

    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    textures.push_back(texture);
    texturesUploadHeaps.push_back(textureUploadHeap);
}