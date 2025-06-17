#pragma once
#include "Meshes.h"
#include <string>
#include "Material.h"

class ModelLoader {
public:
    static Mesh LoadGeometry(const std::string& objPath);
};
