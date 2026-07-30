#include "base_core/core_defs.hpp"
#include <cstdlib>
#include <filesystem>
#include <string>

std::string get_toml_config_directory()
{
  if (const char * env_dir = std::getenv("AEGIS_AUTONOMY_CONFIG_DIR")) {
    if (std::filesystem::exists(env_dir)) {
      return std::string(env_dir);
    }
  }

  return ".";
}
