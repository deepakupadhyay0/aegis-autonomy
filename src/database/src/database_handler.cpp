#include "database/database_handler.hpp"

#include "database/database_config.hpp"

#include <utility>

namespace database
{

database_handler_c::database_handler_c(
  std::unique_ptr<abstract_database_backend_c> backend)
: m_backend(std::move(backend))
{
  if (m_backend == nullptr) {
    throw database_error_c("Database backend cannot be null");
  }
}

database_handler_c::~database_handler_c() noexcept
{
  this->shutdown();
}

database_handler_c & database_handler_c::get_handler()
{
  static database_handler_c handler(
    std::make_unique<sqlite_database_backend_c>(load_database_options()));
  return handler;
}

bool database_handler_c::try_store(database_record_s && record) noexcept
{
  return m_backend != nullptr && m_backend->try_store(std::move(record));
}

database_statistics_s database_handler_c::get_statistics() const noexcept
{
  return m_backend == nullptr ?
    database_statistics_s{} : m_backend->get_statistics();
}

void database_handler_c::shutdown() noexcept
{
  if (m_backend != nullptr) {
    m_backend->shutdown();
  }
}

}  // namespace database
