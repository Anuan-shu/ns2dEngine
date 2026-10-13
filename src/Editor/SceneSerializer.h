#pragma once
#include "SceneData.h"
#include <filesystem>

class SceneSerializer {
public:
    static std::string Encode(const SceneData& scene);
    static SceneData Decode(const std::string& source);
    static void Save(const std::filesystem::path& path, const SceneData& scene);
    static SceneData Load(const std::filesystem::path& path);
};
