#include "database/sqlite_database.hpp"

#include <limits>
#include <utility>

namespace database
{
namespace
{

void check_sqlite_result(
  const common::int32_t result,
  sqlite3 * const connection,
  const char * const operation)
{
  if (result == SQLITE_OK) {
    return;
  }
  std::string message(operation);
  message.append(": ");
  message.append(connection == nullptr ?
    sqlite3_errstr(result) : sqlite3_errmsg(connection));
  throw database_error_c(message);
}

}  // namespace

database_error_c::database_error_c(const std::string & message)
: std::runtime_error(message)
{
}

sqlite_statement_c::sqlite_statement_c(
  sqlite3 & connection,
  const std::string_view sql)
: m_statement(nullptr)
{
  if (sql.size() > static_cast<std::size_t>(
      std::numeric_limits<common::int32_t>::max()))
  {
    throw database_error_c("SQLite statement exceeds supported length");
  }
  sqlite3_stmt * statement = nullptr;
  const common::int32_t result = sqlite3_prepare_v3(
    &connection,
    sql.data(),
    static_cast<common::int32_t>(sql.size()),
    SQLITE_PREPARE_PERSISTENT,
    &statement,
    nullptr);
  check_sqlite_result(result, &connection, "SQLite prepare failed");
  m_statement.reset(statement);
}

sqlite_statement_c::~sqlite_statement_c() noexcept = default;

sqlite_statement_c::sqlite_statement_c(sqlite_statement_c &&) noexcept = default;
sqlite_statement_c & sqlite_statement_c::operator=(sqlite_statement_c &&) noexcept = default;

void sqlite_statement_c::statement_deleter_s::operator()(
  sqlite3_stmt * const statement) const noexcept
{
  if (statement != nullptr) {
    static_cast<void>(sqlite3_finalize(statement));
  }
}

void sqlite_statement_c::bind(
  const common::int32_t index,
  const common::int32_t value)
{
  check_sqlite_result(
    sqlite3_bind_int(m_statement.get(), index, value),
    sqlite3_db_handle(m_statement.get()),
    "SQLite integer bind failed");
}

void sqlite_statement_c::bind(
  const common::int32_t index,
  const common::uint32_t value)
{
  this->bind(index, static_cast<common::int64_t>(value));
}

void sqlite_statement_c::bind(
  const common::int32_t index,
  const common::int64_t value)
{
  check_sqlite_result(
    sqlite3_bind_int64(m_statement.get(), index, value),
    sqlite3_db_handle(m_statement.get()),
    "SQLite 64-bit integer bind failed");
}

void sqlite_statement_c::bind(
  const common::int32_t index,
  const common::uint64_t value)
{
  if (value > static_cast<common::uint64_t>(
      std::numeric_limits<common::int64_t>::max()))
  {
    throw database_error_c("Unsigned value exceeds SQLite INTEGER range");
  }
  this->bind(index, static_cast<common::int64_t>(value));
}

void sqlite_statement_c::bind(
  const common::int32_t index,
  const common::float32_t value)
{
  this->bind(index, static_cast<common::float64_t>(value));
}

void sqlite_statement_c::bind(
  const common::int32_t index,
  const common::float64_t value)
{
  check_sqlite_result(
    sqlite3_bind_double(m_statement.get(), index, value),
    sqlite3_db_handle(m_statement.get()),
    "SQLite floating-point bind failed");
}

void sqlite_statement_c::bind(
  const common::int32_t index,
  const bool value)
{
  this->bind(index, static_cast<common::int32_t>(value ? 1 : 0));
}

void sqlite_statement_c::bind(
  const common::int32_t index,
  const std::string_view value)
{
  check_sqlite_result(
    sqlite3_bind_text64(
      m_statement.get(),
      index,
      value.data(),
      static_cast<sqlite3_uint64>(value.size()),
      SQLITE_STATIC,
      SQLITE_UTF8),
    sqlite3_db_handle(m_statement.get()),
    "SQLite text bind failed");
}

void sqlite_statement_c::bind(
  const common::int32_t index,
  const blob_view_s value)
{
  if (value.size == 0U) {
    check_sqlite_result(
      sqlite3_bind_zeroblob64(m_statement.get(), index, 0U),
      sqlite3_db_handle(m_statement.get()),
      "SQLite empty BLOB bind failed");
    return;
  }
  if (value.data == nullptr && value.size != 0U) {
    throw database_error_c("Non-empty SQLite BLOB has null storage");
  }
  check_sqlite_result(
    sqlite3_bind_blob64(
      m_statement.get(),
      index,
      value.data,
      static_cast<sqlite3_uint64>(value.size),
      SQLITE_STATIC),
    sqlite3_db_handle(m_statement.get()),
    "SQLite BLOB bind failed");
}

void sqlite_statement_c::bind_null(const common::int32_t index)
{
  check_sqlite_result(
    sqlite3_bind_null(m_statement.get(), index),
    sqlite3_db_handle(m_statement.get()),
    "SQLite null bind failed");
}

void sqlite_statement_c::execute()
{
  const common::int32_t result = sqlite3_step(m_statement.get());
  if (result != SQLITE_DONE) {
    check_sqlite_result(
      result,
      sqlite3_db_handle(m_statement.get()),
      "SQLite statement execution failed");
  }
}

void sqlite_statement_c::reset()
{
  const common::int32_t reset_result = sqlite3_reset(m_statement.get());
  const common::int32_t clear_result = sqlite3_clear_bindings(m_statement.get());
  check_sqlite_result(
    reset_result,
    sqlite3_db_handle(m_statement.get()),
    "SQLite statement reset failed");
  check_sqlite_result(
    clear_result,
    sqlite3_db_handle(m_statement.get()),
    "SQLite binding cleanup failed");
}

sqlite_connection_c::sqlite_connection_c(
  const std::string_view path,
  const common::uint32_t busy_timeout_ms)
: m_connection(nullptr)
{
  if (path.empty()) {
    throw database_error_c("SQLite database path cannot be empty");
  }
  if (busy_timeout_ms > static_cast<common::uint32_t>(
      std::numeric_limits<common::int32_t>::max()))
  {
    throw database_error_c("SQLite busy timeout exceeds supported range");
  }
  sqlite3 * connection = nullptr;
  const std::string path_string(path);
  const common::int32_t result = sqlite3_open_v2(
    path_string.c_str(),
    &connection,
    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
    nullptr);
  if (result != SQLITE_OK) {
    const std::string message = connection == nullptr ?
      std::string(sqlite3_errstr(result)) :
      std::string(sqlite3_errmsg(connection));
    if (connection != nullptr) {
      static_cast<void>(sqlite3_close(connection));
    }
    throw database_error_c("SQLite open failed: " + message);
  }
  m_connection.reset(connection);
  check_sqlite_result(
    sqlite3_busy_timeout(
      m_connection.get(),
      static_cast<common::int32_t>(busy_timeout_ms)),
    m_connection.get(),
    "SQLite busy timeout configuration failed");
  this->execute("PRAGMA journal_mode=WAL;");
  this->execute("PRAGMA synchronous=NORMAL;");
  this->execute("PRAGMA foreign_keys=ON;");
}

sqlite_connection_c::~sqlite_connection_c() noexcept = default;

void sqlite_connection_c::connection_deleter_s::operator()(
  sqlite3 * const connection) const noexcept
{
  if (connection != nullptr) {
    static_cast<void>(sqlite3_close_v2(connection));
  }
}

void sqlite_connection_c::execute(const std::string_view sql)
{
  char * error_message = nullptr;
  const std::string sql_string(sql);
  const common::int32_t result = sqlite3_exec(
    m_connection.get(), sql_string.c_str(), nullptr, nullptr, &error_message);
  if (result == SQLITE_OK) {
    return;
  }
  const std::string message = error_message == nullptr ?
    std::string(sqlite3_errmsg(m_connection.get())) :
    std::string(error_message);
  sqlite3_free(error_message);
  throw database_error_c("SQLite execution failed: " + message);
}

sqlite3 & sqlite_connection_c::native_handle() noexcept
{
  return *m_connection;
}

common::int64_t sqlite_connection_c::get_last_insert_id() const noexcept
{
  return sqlite3_last_insert_rowid(m_connection.get());
}

sqlite_transaction_c::sqlite_transaction_c(sqlite_connection_c & connection)
: m_connection(connection), m_committed(false)
{
  m_connection.execute("BEGIN IMMEDIATE;");
}

sqlite_transaction_c::~sqlite_transaction_c() noexcept
{
  if (!m_committed) {
    try {
      m_connection.execute("ROLLBACK;");
    } catch (...) {
    }
  }
}

void sqlite_transaction_c::commit()
{
  m_connection.execute("COMMIT;");
  m_committed = true;
}

}  // namespace database
