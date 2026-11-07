#pragma once
#include "mod_json.h"
#include <filesystem>
#include <map>
#include <string>
namespace mods::content {
using Files=std::map<std::string,std::string>;

json::Value synthesize_cemu_content(const std::filesystem::path& pack,const std::string& folder_name);
Files index(const std::filesystem::path& directory);

void activate(Files files);
void deactivate();
std::string replacement(const std::string& guest, const std::string& mode="rb");
}
