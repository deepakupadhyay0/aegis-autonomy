#include "autonomy_config/config_directory.hpp"

#include <cstdlib>

namespace autonomy_config
{

std::filesystem::path get_config_directory()
{
  const char * const environment_directory =
    std::getenv("AEGIS_AUTONOMY_CONFIG_DIR");
  if (environment_directory == nullptr || environment_directory[0] == '\0') {
    return std::filesystem::path(".");
  }
  return std::filesystem::path(environment_directory);
}

}  // namespace autonomy_config
