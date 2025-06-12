#pragma once
#include "Meshes.h"
#include <string>

class ModelLoader {
public:
    static Mesh LoadGeometry(const std::string& objPath);
};
