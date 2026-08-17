#pragma once

#include "database/database_backend.hpp"
#include "database/database_record.hpp"
#include "database/visibility_control.hpp"

#include <memory>

namespace database
{

/// Lazy process singleton. Callers only enqueue; the backend owns all SQLite
/// work and the sole connection used by this process.
class DATABASE_PUBLIC database_handler_c final
{
public:
  ~database_handler_c() noexcept;

  database_handler_c(const database_handler_c &) = delete;
  database_handler_c & operator=(const database_handler_c &) = delete;
  database_handler_c(database_handler_c &&) = delete;
  database_handler_c & operator=(database_handler_c &&) = delete;

  static database_handler_c & get_handler();

  bool try_store(database_record_s && record) noexcept;
  database_statistics_s get_statistics() const noexcept;
  void shutdown() noexcept;

private:
  explicit database_handler_c(std::unique_ptr<abstract_database_backend_c> backend);

  std::unique_ptr<abstract_database_backend_c> m_backend;
};

}  // namespace database
