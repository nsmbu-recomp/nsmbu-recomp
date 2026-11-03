

#pragma once
#ifdef _WIN32

#include <string>
#include <vector>

std::string bundled_python(const std::string& pkg);
bool have_bundled_python(const std::string& pkg);

int console_setup(const std::string& pkg, const std::vector<std::string>& args);

#endif
