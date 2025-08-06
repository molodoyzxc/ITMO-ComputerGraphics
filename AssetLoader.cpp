#include "AssetLoader.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include <unordered_map>
#include <WICTextureLoader.h>
#include "DX12Framework.h"
#include <d3d12.h>
#include "d3dx12.h"  
#include <DirectXTex.h>
#include <filesystem>
#include "SceneObject.h"
#include <vector>

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

UINT AssetLoader::LoadTexture(ID3D12Device* device, ResourceUploadBatch& uploadBatch, DX12Framework* framework, const wchar_t* filename)
{
    HRESULT hr = S_OK;
    ComPtr<ID3D12Resource> texture;

    std::wstring ext = std::filesystem::path(filename).extension().wstring();
    if (_wcsicmp(ext.c_str(), L".tga") == 0)
    {
        DirectX::ScratchImage scratch;
        hr = DirectX::LoadFromTGAFile(filename, nullptr, scratch);
        if (FAILED(hr)) throw std::runtime_error("LoadFromTGAFile failed");

        auto meta = scratch.GetMetadata();
        hr = DirectX::CreateTexture(
            device,
            meta,
            texture.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) throw std::runtime_error("CreateTexture failed");

        const auto* imgs = scratch.GetImages();
        size_t imgCount = scratch.GetImageCount();
        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        subresources.reserve(imgCount);
        for (size_t i = 0; i < imgCount; ++i)
        {
            D3D12_SUBRESOURCE_DATA d = {};
            d.pData = imgs[i].pixels;
            d.RowPitch = imgs[i].rowPitch;
            d.SlicePitch = imgs[i].slicePitch;
            subresources.push_back(d);
        }

        uploadBatch.Upload(
            texture.Get(),
            0,
            subresources.data(),
            static_cast<UINT>(subresources.size())
        );

        uploadBatch.Transition(
            texture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
    }
    else 
    {
        hr = DirectX::CreateWICTextureFromFile(
            device,
            uploadBatch,
            filename,
            texture.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) {
            throw std::runtime_error("Create texture from file failed");
        }   
    }

    auto desc = texture->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(-1);

    UINT srvIndex = framework->AllocateSrvDescriptor();

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(
        framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
        srvIndex,
        framework->GetSrvDescriptorSize()
    );

    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    textures.push_back(texture);
    return srvIndex;
}

std::vector<SceneObject> AssetLoader::LoadSceneObjects(const std::string& objPath) {
    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string                      warn, err;
    std::string baseDir = std::filesystem::path(objPath)
        .parent_path()
        .string();

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
        objPath.c_str(), baseDir.c_str())) {
        throw std::runtime_error("TinyObjLoader error: " + warn + err);
    }

    size_t M = materials.size();
    std::vector<Mesh> meshPerMat(M);
    std::vector<std::unordered_map<uint64_t, uint32_t>> uniqueVertMaps(M);

    for (auto& shape : shapes) {
        auto& fvCounts = shape.mesh.num_face_vertices;
        auto& matIds = shape.mesh.material_ids;
        auto& idxs = shape.mesh.indices;

        size_t indexOffset = 0;
        for (size_t f = 0; f < fvCounts.size(); ++f) {
            int rawMatId = (f < matIds.size() ? matIds[f] : -1);
            int mid = (rawMatId >= 0 && rawMatId < (int)M) ? rawMatId : 0;

            Mesh& mesh = meshPerMat[mid];
            auto& umap = uniqueVertMaps[mid];

            for (size_t v = 0; v < fvCounts[f]; ++v) {
                const auto& idx = idxs[indexOffset + v];
                uint64_t key = (uint64_t(idx.vertex_index + 1) << 42)
                    | (uint64_t(idx.texcoord_index + 1) << 21)
                    | uint64_t(idx.normal_index + 1);

                uint32_t newIndex;
                auto it = umap.find(key);
                if (it == umap.end()) {
                    Vertex vert{};
                    vert.Pos = {
                        attrib.vertices[3 * idx.vertex_index + 0],
                        attrib.vertices[3 * idx.vertex_index + 1],
                        attrib.vertices[3 * idx.vertex_index + 2]
                    };
                    if (idx.normal_index >= 0) {
                        vert.Normal = {
                            attrib.normals[3 * idx.normal_index + 0],
                            attrib.normals[3 * idx.normal_index + 1],
                            attrib.normals[3 * idx.normal_index + 2]
                        };
                    }
                    if (idx.texcoord_index >= 0) {
                        vert.uv = {
                            attrib.texcoords[2 * idx.texcoord_index + 0],
                            1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]
                        };
                    }
                    newIndex = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(vert);
                    umap[key] = newIndex;
                }
                else {
                    newIndex = it->second;
                }
                mesh.indices.push_back(newIndex);
            }
            indexOffset += fvCounts[f];
        }
    }

    for (auto& mesh : meshPerMat) {
        for (auto& v : mesh.vertices) {
            v.tangent = { 0,0,0 };
            v.handedness = 0;
        }

        for (size_t f = 0; f + 2 < mesh.indices.size(); f += 3) {
            uint32_t i0 = mesh.indices[f], i1 = mesh.indices[f + 1], i2 = mesh.indices[f + 2];
            auto& v0 = mesh.vertices[i0];
            auto& v1 = mesh.vertices[i1];
            auto& v2 = mesh.vertices[i2];

            float duv1x = v1.uv.x - v0.uv.x;
            float duv1y = v1.uv.y - v0.uv.y;
            float duv2x = v2.uv.x - v0.uv.x;
            float duv2y = v2.uv.y - v0.uv.y;

            float det = duv1x * duv2y - duv2x * duv1y;
            XMVECTOR T, B;
            float H;

            if (fabs(det) < 1e-6f) {
                T = XMVectorSet(1, 0, 0, 0);
                B = XMVectorSet(0, 1, 0, 0);
                H = 1.0f;
            }
            else {
                float invDet = 1.0f / det;
                XMVECTOR p0 = XMLoadFloat3(&v0.Pos);
                XMVECTOR p1 = XMLoadFloat3(&v1.Pos);
                XMVECTOR p2 = XMLoadFloat3(&v2.Pos);
                XMVECTOR edge1 = p1 - p0;
                XMVECTOR edge2 = p2 - p0;

                T = (edge1 * duv2y - edge2 * duv1y) * invDet;
                B = (edge2 * duv1x - edge1 * duv2x) * invDet;
                H = XMVectorGetX(XMVector3Dot(XMVector3Cross(edge1, edge2), B)) < 0 ? -1.0f : 1.0f;
            }

            XMVECTOR t0 = XMVector3Normalize(XMLoadFloat3(&v0.tangent) + T);
            XMVECTOR t1 = XMVector3Normalize(XMLoadFloat3(&v1.tangent) + T);
            XMVECTOR t2 = XMVector3Normalize(XMLoadFloat3(&v2.tangent) + T);

            XMStoreFloat3(&v0.tangent, t0);
            XMStoreFloat3(&v1.tangent, t1);
            XMStoreFloat3(&v2.tangent, t2);

            v0.handedness = v1.handedness = v2.handedness = H;
        }

        for (auto& v : mesh.vertices) {
            XMVECTOR t = XMLoadFloat3(&v.tangent);
            t = XMVector3Normalize(t);
            XMStoreFloat3(&v.tangent, t);
        }
    }

    std::vector<SceneObject> sceneObjects;
    sceneObjects.reserve(M);

    for (size_t i = 0; i < M; ++i) {
        if (meshPerMat[i].indices.empty())
            continue;

        SceneObject obj(
            meshPerMat[i],
            { 0.0f,0.0f,0.0f },
            { 0.0f,0.0f,0.0f },
            { 1.0f,1.0f,1.0f }
        );

        const auto& m = materials[i];
        obj.material.ambient = { m.ambient[0],   m.ambient[1],   m.ambient[2] };
        obj.material.diffuse = { m.diffuse[0],   m.diffuse[1],   m.diffuse[2] };
        obj.material.specular = { m.specular[0],  m.specular[1],  m.specular[2] };
        obj.material.shininess = m.shininess;
        obj.material.diffuseTexPath = m.diffuse_texname;
        obj.material.normalTexPath =
            !m.bump_texname.empty() ? m.bump_texname
            : !m.normal_texname.empty() ? m.normal_texname
            : "";

        obj.material.displacementTexPath =
            !m.displacement_texname.empty() ? m.displacement_texname
            : !m.displacement_texname.empty() ? m.displacement_texname
            : "";

        obj.material.roughnessTexPath = m.roughness_texname;
        obj.material.metallicTexPath = m.metallic_texname;
        obj.material.aoTexPath = !m.ambient_texname.empty()
            ? m.ambient_texname
            : m.ambient_texname;

        sceneObjects.push_back(std::move(obj));
    }

    return sceneObjects;
}