#pragma once

#include "database/sqlite_database.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace database
{
namespace orm
{

template<typename owner_t, typename field_t, bool identity_v>
struct column_s
{
  static constexpr bool IS_IDENTITY = identity_v;
  std::string_view name;
  field_t owner_t::* member;
};

template<typename owner_t, typename field_t>
constexpr column_s<owner_t, field_t, false> column(
  const std::string_view name,
  field_t owner_t::* const member) noexcept
{
  return column_s<owner_t, field_t, false>{name, member};
}

template<typename owner_t, typename field_t>
constexpr column_s<owner_t, field_t, true> identity_column(
  const std::string_view name,
  field_t owner_t::* const member) noexcept
{
  return column_s<owner_t, field_t, true>{name, member};
}

template<typename entry_t>
struct table_traits_s;

template<typename value_t>
struct optional_traits_s
{
  static constexpr bool IS_OPTIONAL = false;
  using value_type = value_t;
};

template<typename value_t>
struct optional_traits_s<std::optional<value_t>>
{
  static constexpr bool IS_OPTIONAL = true;
  using value_type = value_t;
};

template<typename value_t>
struct is_fixed_string_s : std::false_type
{
};

template<std::size_t capacity_v>
struct is_fixed_string_s<common::fixed_string_c<capacity_v>> : std::true_type
{
};

template<typename value_t>
std::string_view sqlite_type()
{
  using optional_traits_t = optional_traits_s<std::remove_cv_t<value_t>>;
  using field_t = typename optional_traits_t::value_type;
  if constexpr (std::is_integral_v<field_t>) {
    return "INTEGER";
  } else if constexpr (std::is_floating_point_v<field_t>) {
    return "REAL";
  } else if constexpr (is_fixed_string_s<field_t>::value) {
    return "TEXT";
  } else if constexpr (std::is_same_v<field_t, blob_view_s>) {
    return "BLOB";
  } else {
    static_assert(!sizeof(field_t), "Unsupported ORM field type");
  }
}

inline void append_identifier(
  std::string & sql,
  const std::string_view identifier)
{
  if (identifier.empty()) {
    throw database_error_c("SQLite identifier cannot be empty");
  }
  for (const char character : identifier) {
    const bool valid =
      (character >= 'a' && character <= 'z') ||
      (character >= 'A' && character <= 'Z') ||
      (character >= '0' && character <= '9') || character == '_';
    if (!valid) {
      throw database_error_c("SQLite identifier contains invalid characters");
    }
  }
  sql.append(identifier);
}

template<typename entry_t>
std::string make_create_table_sql()
{
  std::string sql("CREATE TABLE IF NOT EXISTS ");
  append_identifier(sql, table_traits_s<entry_t>::TABLE_NAME);
  sql.append(" (");
  bool first = true;
  std::apply(
    [&sql, &first](const auto & ... columns) {
      const auto append_column = [&sql, &first](const auto & descriptor) {
          if (!first) {
            sql.append(",");
          }
          first = false;
          append_identifier(sql, descriptor.name);
          sql.append(" ");
          using field_t = std::remove_reference_t<decltype(
                std::declval<entry_t>().*descriptor.member)>;
          sql.append(sqlite_type<field_t>());
          if constexpr (std::remove_cvref_t<decltype(descriptor)>::IS_IDENTITY) {
            sql.append(" PRIMARY KEY AUTOINCREMENT");
          } else if constexpr (!optional_traits_s<field_t>::IS_OPTIONAL) {
            sql.append(" NOT NULL");
          }
        };
      (append_column(columns), ...);
    },
    table_traits_s<entry_t>::COLUMNS);
  sql.append(");");
  return sql;
}

template<typename entry_t>
std::string make_insert_sql()
{
  std::string columns_sql;
  std::string values_sql;
  bool first = true;
  std::apply(
    [&columns_sql, &values_sql, &first](const auto & ... columns) {
      const auto append_column =
        [&columns_sql, &values_sql, &first](const auto & descriptor) {
          if constexpr (std::remove_cvref_t<decltype(descriptor)>::IS_IDENTITY) {
            return;
          }
          if (!first) {
            columns_sql.append(",");
            values_sql.append(",");
          }
          first = false;
          append_identifier(columns_sql, descriptor.name);
          values_sql.append("?");
        };
      (append_column(columns), ...);
    },
    table_traits_s<entry_t>::COLUMNS);

  std::string sql("INSERT INTO ");
  append_identifier(sql, table_traits_s<entry_t>::TABLE_NAME);
  sql.append(" (");
  sql.append(columns_sql);
  sql.append(") VALUES (");
  sql.append(values_sql);
  sql.append(");");
  return sql;
}

template<typename value_t>
void bind_value(
  sqlite_statement_c & statement,
  const common::int32_t index,
  const value_t & value)
{
  if constexpr (optional_traits_s<value_t>::IS_OPTIONAL) {
    if (value.has_value()) {
      statement.bind(index, *value);
    } else {
      statement.bind_null(index);
    }
  } else {
    statement.bind(index, value);
  }
}

template<typename entry_t>
class prepared_inserter_c final
{
public:
  explicit prepared_inserter_c(sqlite_connection_c & connection)
  : m_connection(connection),
    m_statement(connection.native_handle(), make_insert_sql<entry_t>())
  {
  }

  void insert(entry_t & entry)
  {
    try {
      common::int32_t index = 1;
      std::apply(
        [this, &entry, &index](const auto & ... columns) {
          const auto bind_column =
            [this, &entry, &index](const auto & descriptor) {
              if constexpr (!std::remove_cvref_t<
                  decltype(descriptor)>::IS_IDENTITY)
              {
                bind_value(m_statement, index, entry.*descriptor.member);
                ++index;
              }
            };
          (bind_column(columns), ...);
        },
        table_traits_s<entry_t>::COLUMNS);
      m_statement.execute();
    } catch (...) {
      try {
        m_statement.reset();
      } catch (...) {
      }
      throw;
    }
    std::apply(
      [this, &entry](const auto & ... columns) {
        const auto set_identity = [this, &entry](const auto & descriptor) {
            if constexpr (std::remove_cvref_t<decltype(descriptor)>::IS_IDENTITY) {
              entry.*descriptor.member = static_cast<
                std::remove_reference_t<decltype(entry.*descriptor.member)>>(
                m_connection.get_last_insert_id());
            }
          };
        (set_identity(columns), ...);
      },
      table_traits_s<entry_t>::COLUMNS);
    m_statement.reset();
  }

private:
  sqlite_connection_c & m_connection;
  sqlite_statement_c m_statement;
};

template<typename entry_t>
void create_table(sqlite_connection_c & connection)
{
  connection.execute(make_create_table_sql<entry_t>());
}

}  // namespace orm
}  // namespace database
