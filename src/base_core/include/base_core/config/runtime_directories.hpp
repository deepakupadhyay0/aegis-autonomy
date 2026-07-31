#pragma once

#include "base_core/visibility_control.hpp"

#include <filesystem>
#include <string>

namespace base_core
{
namespace config
{

BASE_CORE_PUBLIC std::filesystem::path get_config_directory();

}  // namespace config
}  // namespace base_core

/// Strong override for autonomy_config's generated weak directory hook.
BASE_CORE_PUBLIC std::string get_toml_config_directory();
