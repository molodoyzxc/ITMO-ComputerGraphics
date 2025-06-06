#pragma once

#include <string>
#include "Model.h"
#include "DX12Framework.h"

// ≈сли используете ResourceUploadBatch дл€ загрузки текстур
#include <DirectXHelpers.h>
#include <WICTextureLoader.h>

// ѕишем класс-утилиту:
class ModelLoader {
public:
    // «агружаем из файлов .obj и .mtl, использу€ ResourceUploadBatch дл€ текстур
    // objPath Ч полный путь к .obj, framework Ч указатель на DX12Framework,
    // uploadBatch Ч инициированный ресурсный батч, scaleUV Ч коэффициент масштабировани€ UV
    static bool LoadModelFromFile(
        const std::wstring& objPath,
        DX12Framework* framework,
        DirectX::ResourceUploadBatch& uploadBatch,
        Model& outModel
    );
};
