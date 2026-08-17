#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"
#include "database/visibility_control.hpp"

namespace database
{

struct database_options_s
{
  common::string256_t database_path;
  common::uint32_t queue_capacity{0U};
  common::uint32_t batch_size{0U};
  common::uint32_t busy_timeout_ms{0U};
  common::uint32_t max_write_retries{0U};
};

DATABASE_PUBLIC database_options_s load_database_options();

}  // namespace database
