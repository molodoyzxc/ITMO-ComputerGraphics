#include "ModelLoader.h"
#include "MeshPart.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include <unordered_map>
#include <filesystem>
#include <stdexcept>
#include <DirectXMath.h>
#include "d3dx12.h"
#include <wrl.h>

using namespace tinyobj;
using Microsoft::WRL::ComPtr;

// Вспомогательная функция для проверки HRESULT
static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed in ModelLoader");
}

// Вспомогательная функция: загружает одну текстуру и возвращает индекс SRV
static UINT LoadTextureAndCreateSRV(
    DX12Framework* framework,
    DirectX::ResourceUploadBatch& uploadBatch,
    const std::wstring& texPathW // полный путь к текстуре в wchar
) {
    // Если файл не существует, возвращаем UINT_MAX
    if (!std::filesystem::exists(texPathW)) {
        return UINT_MAX;
    }

    ID3D12Device* device = framework->GetDevice();

    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> textureUploadHeap;
    HRESULT hr = DirectX::CreateWICTextureFromFile(
        device,
        uploadBatch,
        texPathW.c_str(),
        texture.GetAddressOf(),
        textureUploadHeap.GetAddressOf()
    );
    if (FAILED(hr)) {
        return UINT_MAX;
    }

    // Создаём SRV-дескриптор
    UINT srvIndex = framework->AllocateSrvDescriptor();

    // Формат текстуры:
    auto desc = texture->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(
        framework->GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
        srvIndex,
        framework->GetSrvDescriptorSize()
    );
    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    return srvIndex;
}

bool ModelLoader::LoadModelFromFile(
    const std::wstring& objPath,
    DX12Framework* framework,
    DirectX::ResourceUploadBatch& uploadBatch,
    Model& outModel
) {
    // 1. Определяем директорию, чтобы строить относительные пути к MTL и текстурам
    std::filesystem::path objFile(objPath);
    std::filesystem::path baseDirPath = objFile.parent_path();

    // tinyobjloader ожидает UTF-8 пути, берём generic_u8string (поддерживается в C++20)
    std::string baseDir = baseDirPath.generic_u8string();

    // 2. Парсим OBJ + MTL
    attrib_t attrib;
    std::vector<shape_t> shapes;
    std::vector<material_t> materialsTiny;
    std::string warn, err;

    // Для objPath тоже нужен UTF-8
    std::string objPathA = objFile.generic_u8string();

    bool ret = LoadObj(
        &attrib,
        &shapes,
        &materialsTiny,
        &warn,
        &err,
        objPathA.c_str(),
        baseDir.c_str()
    );

    if (!warn.empty()) {
        OutputDebugStringA(("tinyobj warning: " + warn + "\n").c_str());
    }
    if (!err.empty()) {
        OutputDebugStringA(("tinyobj error: " + err + "\n").c_str());
    }
    if (!ret) {
        return false;
    }

    // 3. Копируем tinyobj-material_t ? наш Material (из Material.h)
    outModel.materials.resize(materialsTiny.size());
    for (size_t i = 0; i < materialsTiny.size(); ++i) {
        const auto& matTiny = materialsTiny[i];
        Material mat;
        mat.name = matTiny.name;

        // Цвета
        mat.diffuseColor = { matTiny.diffuse[0], matTiny.diffuse[1], matTiny.diffuse[2] };
        mat.specularColor = { matTiny.specular[0], matTiny.specular[1], matTiny.specular[2] };
        mat.shininess = matTiny.shininess;

        // Пути к текстурам делаем через baseDir + tinyobj-поле
        if (!matTiny.diffuse_texname.empty()) {
            std::filesystem::path p = baseDirPath / matTiny.diffuse_texname;
            mat.albedoTexPath = p.wstring();
        }
        if (!matTiny.bump_texname.empty()) {
            std::filesystem::path p = baseDirPath / matTiny.bump_texname;
            mat.normalTexPath = p.wstring();
        }

        outModel.materials[i] = mat;
    }

    // 4. Загружаем текстуры для каждого материала через uploadBatch
    for (size_t i = 0; i < outModel.materials.size(); ++i) {
        Material& mat = outModel.materials[i];
        if (!mat.albedoTexPath.empty()) {
            mat.albedoSrvIndex = LoadTextureAndCreateSRV(framework, uploadBatch, mat.albedoTexPath);
        }
        if (!mat.normalTexPath.empty()) {
            mat.normalSrvIndex = LoadTextureAndCreateSRV(framework, uploadBatch, mat.normalTexPath);
        }
        if (!mat.metallicRoughTexPath.empty()) {
            mat.metallicRoughSrvIndex = LoadTextureAndCreateSRV(framework, uploadBatch, mat.metallicRoughTexPath);
        }
    }

    // 5. Для каждой shape формируем MeshPart
    outModel.parts.clear();
    outModel.parts.reserve(shapes.size());

    // Вспомогательные структуры для устранения дублирования вершин
    struct PackedVertex {
        int vIdx, vtIdx, vnIdx;
        bool operator==(const PackedVertex& o) const {
            return vIdx == o.vIdx && vtIdx == o.vtIdx && vnIdx == o.vnIdx;
        }
    };
    struct VertexHash {
        size_t operator()(PackedVertex const& pv) const {
            return (std::hash<int>()(pv.vIdx) * 73856093) ^
                (std::hash<int>()(pv.vtIdx) * 19349663) ^
                (std::hash<int>()(pv.vnIdx) * 83492791);
        }
    };

    // Лямбда для получения Vertex по индексам tinyobj
    auto GetVertexFromAttrib = [&](int vIdx, int vtIdx, int vnIdx) {
        DirectX::XMFLOAT3 pos = {
            attrib.vertices[3 * vIdx + 0],
            attrib.vertices[3 * vIdx + 1],
            attrib.vertices[3 * vIdx + 2]
        };
        DirectX::XMFLOAT3 norm = { 0, 0, 0 };
        if (vnIdx >= 0) {
            norm = {
                attrib.normals[3 * vnIdx + 0],
                attrib.normals[3 * vnIdx + 1],
                attrib.normals[3 * vnIdx + 2]
            };
        }
        DirectX::XMFLOAT2 uv = { 0, 0 };
        if (vtIdx >= 0) {
            uv = {
                attrib.texcoords[2 * vtIdx + 0],
                1.0f - attrib.texcoords[2 * vtIdx + 1] // переворт V
            };
        }
        return Vertex{ pos, norm, uv };
        };

    // Проходим по каждому shape
    for (const auto& shape : shapes) {
        std::unordered_map<PackedVertex, UINT, VertexHash> uniqueVertices;
        std::vector<Vertex> vertices;
        std::vector<UINT32> indices32;

        // tinyobj сейчас хранит material_id не в index_t, а в отдельном векторе material_ids (по три индекса на треугольник).
        // Значит, shape.mesh.material_ids имеет размер = shape.mesh.num_face_vertices.size()
        // и содержит material_id для каждой грани.
        // Но здесь мы упрощённо назначим материал первой вершине первого треугольника.

        // Перебираем все индексы shape.mesh.indices
        for (size_t f = 0, idxOffset = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            int fv = shape.mesh.num_face_vertices[f]; // обычно 3
            int matID_for_face = UINT_MAX;
            if (f < shape.mesh.material_ids.size()) {
                matID_for_face = shape.mesh.material_ids[f];
            }
            // Проходим по вершинам грани
            for (int v = 0; v < fv; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[idxOffset + v];
                PackedVertex pv{ idx.vertex_index, idx.texcoord_index, idx.normal_index };
                auto it = uniqueVertices.find(pv);
                if (it == uniqueVertices.end()) {
                    UINT newIndex = static_cast<UINT>(vertices.size());
                    Vertex vert = GetVertexFromAttrib(pv.vIdx, pv.vtIdx, pv.vnIdx);
                    vertices.push_back(vert);
                    uniqueVertices[pv] = newIndex;
                    indices32.push_back(newIndex);
                }
                else {
                    indices32.push_back(it->second);
                }
            }
            idxOffset += fv;
        }

        // Создаём MeshPart
        MeshPart part;
        part.vertices = std::move(vertices);
        part.indices = std::move(indices32);
        part.indexCount = static_cast<UINT>(part.indices.size());

        // Определяем материал для этой части (берём первый материал, присвоенный граням)
        if (!shape.mesh.material_ids.empty()) {
            int matIdx = shape.mesh.material_ids[0];
            if (matIdx >= 0 && matIdx < static_cast<int>(outModel.materials.size())) {
                part.materialID = static_cast<UINT>(matIdx);
            }
        }

        // 6. Строим GPU-буферы для этой части
        part.BuildBuffers(framework->GetDevice(), framework->GetCommandList());

        outModel.parts.push_back(std::move(part));
    }

    return true;
}
