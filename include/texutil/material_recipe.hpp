#pragma once
#include "texutil.hpp"
namespace tex {
struct MaterialRecipe { Json document,material; std::filesystem::path base; std::string name; };
Json parseMaterialBinding(Json value, const std::filesystem::path& base, bool bake=false);
MaterialRecipe loadMaterialRecipe(const Json& binding, const std::vector<std::string>& extraTextures={});
Json parseExportBake(const Json& value, const std::filesystem::path& base);
Json exportBake(const Json& settings, const std::filesystem::path& manifest, const Options& options);
}
