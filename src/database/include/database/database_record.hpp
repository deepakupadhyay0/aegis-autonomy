#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"
#include "database/orm.hpp"

#include <cstddef>
#include <memory>
#include <tuple>
#include <vector>

namespace database
{

using payload_storage_t = std::vector<std::byte>;

struct database_record_s
{
  common::int64_t timestamp_ns{0};
  common::uint64_t sequence{0U};
  common::string64_t source;
  common::string64_t record_type;
  common::string16_t encoding;
  std::shared_ptr<const payload_storage_t> payload;
};

struct database_entry_s
{
  common::int64_t id{0};
  common::int64_t timestamp_ns{0};
  common::uint64_t sequence{0U};
  common::string64_t source;
  common::string64_t record_type;
  common::string16_t encoding;
  blob_view_s payload;
};

namespace orm
{

template<>
struct table_traits_s<database_entry_s>
{
  static constexpr std::string_view TABLE_NAME{"data_records"};
  static constexpr auto COLUMNS = std::tuple{
    identity_column("id", &database_entry_s::id),
    column("timestamp_ns", &database_entry_s::timestamp_ns),
    column("sequence", &database_entry_s::sequence),
    column("source", &database_entry_s::source),
    column("record_type", &database_entry_s::record_type),
    column("encoding", &database_entry_s::encoding),
    column("payload", &database_entry_s::payload)};
};

}  // namespace orm
}  // namespace database
