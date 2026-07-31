#pragma once

#include <filesystem>

namespace autonomy_config
{

/// Returns the directory containing runtime TOML configuration files.
std::filesystem::path get_config_directory();

}  // namespace autonomy_config
