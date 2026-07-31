#include "base_core/config/runtime_directories.hpp"

#include "base_core/core_defs.hpp"

#include <cstdlib>
#include <system_error>

namespace base_core
{
namespace config
{

std::filesystem::path get_config_directory()
{
  const char * const environment_directory =
    std::getenv("AEGIS_AUTONOMY_CONFIG_DIR");
  if (environment_directory == nullptr ||
    environment_directory[0] == '\0')
  {
    return std::filesystem::path(".");
  }

  std::error_code filesystem_error;
  const bool8_t is_directory = std::filesystem::is_directory(
    environment_directory,
    filesystem_error);
  return !filesystem_error && is_directory ?
    std::filesystem::path(environment_directory) :
    std::filesystem::path(".");
}

}  // namespace config
}  // namespace base_core

std::string get_toml_config_directory()
{
  return base_core::config::get_config_directory().string();
}
