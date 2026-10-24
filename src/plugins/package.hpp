#pragma once
#include "manifest.hpp"

namespace plugins {
std::string readFile(const std::filesystem::path& file, size_t limit);
// Extract into a new, empty staging directory; never into a live installation.
Manifest extractPackage(const std::filesystem::path& zip, const std::filesystem::path& staging);
Manifest inspectPackage(const std::filesystem::path& zip);
void validateDll(const std::filesystem::path& dll);
}
