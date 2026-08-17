#pragma once

#include "common/bounded_mpsc_queue.hpp"
#include "common/numeric_types.hpp"
#include "database/database_config.hpp"
#include "database/database_record.hpp"
#include "database/visibility_control.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace database
{

struct database_statistics_s
{
  common::uint64_t accepted{0U};
  common::uint64_t rejected{0U};
  common::uint64_t dropped{0U};
  common::uint64_t committed{0U};
  common::uint64_t failed{0U};
};

class DATABASE_PUBLIC abstract_database_backend_c
{
public:
  virtual ~abstract_database_backend_c() noexcept = default;

  abstract_database_backend_c(const abstract_database_backend_c &) = delete;
  abstract_database_backend_c & operator=(const abstract_database_backend_c &) = delete;
  abstract_database_backend_c(abstract_database_backend_c &&) = delete;
  abstract_database_backend_c & operator=(abstract_database_backend_c &&) = delete;

  virtual bool try_store(database_record_s && record) noexcept = 0;
  virtual database_statistics_s get_statistics() const noexcept = 0;
  virtual void shutdown() noexcept = 0;

protected:
  abstract_database_backend_c() noexcept = default;
};

class DATABASE_PUBLIC sqlite_database_backend_c final :
  public abstract_database_backend_c
{
public:
  explicit sqlite_database_backend_c(const database_options_s & options);
  ~sqlite_database_backend_c() noexcept override;

  sqlite_database_backend_c(const sqlite_database_backend_c &) = delete;
  sqlite_database_backend_c & operator=(const sqlite_database_backend_c &) = delete;
  sqlite_database_backend_c(sqlite_database_backend_c &&) = delete;
  sqlite_database_backend_c & operator=(sqlite_database_backend_c &&) = delete;

  bool try_store(database_record_s && record) noexcept override;
  database_statistics_s get_statistics() const noexcept override;
  void shutdown() noexcept override;

private:
  void worker_loop() noexcept;
  bool insert_batch(std::vector<database_record_s> & batch) noexcept;

  database_options_s m_options;
  common::bounded_mpsc_queue_c<database_record_s> m_queue;
  sqlite_connection_c m_connection;
  std::unique_ptr<orm::prepared_inserter_c<database_entry_s>> m_inserter;
  std::vector<database_record_s> m_batch;
  /// Counters are independent diagnostics and use relaxed ordering. Running
  /// uses acquire/release to coordinate idempotent shutdown.
  std::atomic<common::uint64_t> m_accepted;
  std::atomic<common::uint64_t> m_rejected;
  std::atomic<common::uint64_t> m_committed;
  std::atomic<common::uint64_t> m_failed;
  std::atomic<bool> m_running;
  std::jthread m_worker;
};

}  // namespace database
