/**
 * @file codegen.hpp
 * @brief Emits a metasprite frames as a generated C++ header: pattern bytes
 *        only, one row per frame, slots row-major and ready for OAM.
 */
#pragma once

#include "project.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace codegen {

constexpr const char* kHeaderExtension = ".hpp";

constexpr const char* kNamespace = "gen";

constexpr int kNoTile = 0xff;

std::string identifier(const std::string& name);

std::string header(const proj::Metasprite& metasprite, const std::string& source = {});

bool writeHeader(const proj::Metasprite& metasprite, const std::filesystem::path& file,
                 const std::string& source, std::string& error);

bool writeAll(const proj::Project& project, const std::filesystem::path& dir,
              std::vector<std::string>& skipped, std::string& error);

} // namespace codegen
