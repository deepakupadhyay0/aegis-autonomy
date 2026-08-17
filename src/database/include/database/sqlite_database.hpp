#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"
#include "database/visibility_control.hpp"

#include <sqlite3.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace database
{

struct blob_view_s
{
  const std::byte * data{nullptr};
  std::size_t size{0U};
};

class DATABASE_PUBLIC database_error_c final : public std::runtime_error
{
public:
  explicit database_error_c(const std::string & message);
};

class DATABASE_PUBLIC sqlite_statement_c final
{
public:
  sqlite_statement_c(sqlite3 & connection, std::string_view sql);
  ~sqlite_statement_c() noexcept;

  sqlite_statement_c(const sqlite_statement_c &) = delete;
  sqlite_statement_c & operator=(const sqlite_statement_c &) = delete;
  sqlite_statement_c(sqlite_statement_c &&) noexcept;
  sqlite_statement_c & operator=(sqlite_statement_c &&) noexcept;

  void bind(common::int32_t index, common::int32_t value);
  void bind(common::int32_t index, common::uint32_t value);
  void bind(common::int32_t index, common::int64_t value);
  void bind(common::int32_t index, common::uint64_t value);
  void bind(common::int32_t index, common::float32_t value);
  void bind(common::int32_t index, common::float64_t value);
  void bind(common::int32_t index, bool value);
  void bind(common::int32_t index, std::string_view value);
  void bind(common::int32_t index, blob_view_s value);
  void bind_null(common::int32_t index);

  template<std::size_t capacity_v>
  void bind(
    const common::int32_t index,
    const common::fixed_string_c<capacity_v> & value)
  {
    this->bind(index, value.view());
  }

  void execute();
  void reset();

private:
  struct statement_deleter_s
  {
    void operator()(sqlite3_stmt * statement) const noexcept;
  };

  std::unique_ptr<sqlite3_stmt, statement_deleter_s> m_statement;
};

class DATABASE_PUBLIC sqlite_connection_c final
{
public:
  sqlite_connection_c(std::string_view path, common::uint32_t busy_timeout_ms);
  ~sqlite_connection_c() noexcept;

  sqlite_connection_c(const sqlite_connection_c &) = delete;
  sqlite_connection_c & operator=(const sqlite_connection_c &) = delete;
  sqlite_connection_c(sqlite_connection_c &&) = delete;
  sqlite_connection_c & operator=(sqlite_connection_c &&) = delete;

  void execute(std::string_view sql);
  sqlite3 & native_handle() noexcept;
  common::int64_t get_last_insert_id() const noexcept;

private:
  struct connection_deleter_s
  {
    void operator()(sqlite3 * connection) const noexcept;
  };

  std::unique_ptr<sqlite3, connection_deleter_s> m_connection;
};

class DATABASE_PUBLIC sqlite_transaction_c final
{
public:
  explicit sqlite_transaction_c(sqlite_connection_c & connection);
  ~sqlite_transaction_c() noexcept;

  sqlite_transaction_c(const sqlite_transaction_c &) = delete;
  sqlite_transaction_c & operator=(const sqlite_transaction_c &) = delete;
  sqlite_transaction_c(sqlite_transaction_c &&) = delete;
  sqlite_transaction_c & operator=(sqlite_transaction_c &&) = delete;

  void commit();

private:
  sqlite_connection_c & m_connection;
  bool m_committed;
};

}  // namespace database
