#include "database/database_config.hpp"

#include "autonomy_config/config_directory.hpp"

#include <autonomy_config/database.hpp>

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace database
{
namespace
{

common::uint32_t positive_uint32(
  const common::int64_t value,
  const char * const message)
{
  if (value <= 0 || static_cast<common::uint64_t>(value) >
    std::numeric_limits<common::uint32_t>::max())
  {
    throw std::invalid_argument(message);
  }
  return static_cast<common::uint32_t>(value);
}

common::string256_t resolve_database_path(const std::string & configured_path)
{
  if (configured_path.empty()) {
    throw std::invalid_argument("Configured database path is empty");
  }
  std::error_code filesystem_error;
  std::filesystem::path path(configured_path);
  if (path.is_relative()) {
    path = autonomy_config::get_config_directory() / path;
  }
  path = std::filesystem::absolute(path, filesystem_error).lexically_normal();
  if (filesystem_error || path.filename().empty()) {
    throw std::runtime_error("Failed to resolve configured database path");
  }
  std::filesystem::create_directories(path.parent_path(), filesystem_error);
  if (filesystem_error) {
    throw std::runtime_error("Failed to create configured database directory");
  }
  const std::string path_string = path.string();
  if (path_string.size() > common::string256_t::capacity()) {
    throw std::invalid_argument("Configured database path is too long");
  }
  common::string256_t result;
  result.assign(path_string);
  return result;
}

}  // namespace

database_options_s load_database_options()
{
  const autonomy_config::Database config =
    autonomy_config::Database::get_run_time_values();
  database_options_s options;
  options.database_path = resolve_database_path(config.get_database_path());
  options.queue_capacity = positive_uint32(
    config.get_queue_capacity(), "Database queue capacity is invalid");
  options.batch_size = positive_uint32(
    config.get_batch_size(), "Database batch size is invalid");
  if (options.batch_size > options.queue_capacity) {
    throw std::invalid_argument(
            "Database batch size exceeds queue capacity");
  }
  options.busy_timeout_ms = positive_uint32(
    config.get_busy_timeout_ms(), "Database busy timeout is invalid");
  if (options.busy_timeout_ms > static_cast<common::uint32_t>(
      std::numeric_limits<common::int32_t>::max()))
  {
    throw std::invalid_argument("Database busy timeout exceeds SQLite range");
  }
  if (config.get_max_write_retries() < 0 ||
    static_cast<common::uint64_t>(config.get_max_write_retries()) >
    std::numeric_limits<common::uint32_t>::max())
  {
    throw std::invalid_argument("Database write retry count is invalid");
  }
  options.max_write_retries = static_cast<common::uint32_t>(
    config.get_max_write_retries());
  return options;
}

}  // namespace database
