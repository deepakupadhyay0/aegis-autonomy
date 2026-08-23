#include "database/database_backend.hpp"

#include <limits>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

namespace database
{
namespace
{

database_options_s validate_options(const database_options_s & options)
{
  if (options.database_path.empty()) {
    throw std::invalid_argument("Database path cannot be empty");
  }
  if (options.queue_capacity == 0U) {
    throw std::invalid_argument("Database queue capacity must be positive");
  }
  if (options.batch_size == 0U ||
    options.batch_size > options.queue_capacity)
  {
    throw std::invalid_argument("Database batch size is invalid");
  }
  if (options.busy_timeout_ms == 0U ||
    options.busy_timeout_ms > static_cast<common::uint32_t>(
      std::numeric_limits<common::int32_t>::max()))
  {
    throw std::invalid_argument("Database busy timeout is invalid");
  }
  return options;
}

std::unique_ptr<orm::prepared_inserter_c<database_entry_s>> create_inserter(
  sqlite_connection_c & connection)
{
  orm::create_table<database_entry_s>(connection);
  return std::make_unique<orm::prepared_inserter_c<database_entry_s>>(
    connection);
}

std::vector<database_record_s> create_batch(const std::size_t capacity)
{
  std::vector<database_record_s> batch;
  batch.reserve(capacity);
  return batch;
}

void configure_writer_thread() noexcept
{
  static_cast<void>(::pthread_setname_np(::pthread_self(), "database_writer"));
  sched_param parameters{};
  parameters.sched_priority = 0;
  static_cast<void>(
    ::pthread_setschedparam(::pthread_self(), SCHED_OTHER, &parameters));
#ifdef SYS_gettid
  const pid_t thread_id = static_cast<pid_t>(::syscall(SYS_gettid));
  static_cast<void>(::setpriority(PRIO_PROCESS, thread_id, 10));
#endif
}

}  // namespace

sqlite_database_backend_c::sqlite_database_backend_c(
  const database_options_s & options)
: m_options(validate_options(options)),
  m_queue(m_options.queue_capacity),
  m_connection(m_options.database_path.view(), m_options.busy_timeout_ms),
  m_inserter(create_inserter(m_connection)),
  m_batch(create_batch(m_options.batch_size)),
  m_accepted(0U),
  m_rejected(0U),
  m_committed(0U),
  m_failed(0U),
  m_running(true),
  m_worker([this]() noexcept {this->worker_loop();})
{
}

sqlite_database_backend_c::~sqlite_database_backend_c() noexcept
{
  this->shutdown();
}

bool sqlite_database_backend_c::try_store(database_record_s && record) noexcept
{
  if (record.timestamp_ns < 0 || record.source.empty() ||
    record.record_type.empty() || record.encoding.empty() ||
    record.payload == nullptr ||
    record.sequence > static_cast<common::uint64_t>(
      std::numeric_limits<common::int64_t>::max()))
  {
    m_rejected.fetch_add(1U, std::memory_order_relaxed);
    return false;
  }
  if (!m_queue.try_push(std::move(record))) {
    return false;
  }
  m_accepted.fetch_add(1U, std::memory_order_relaxed);
  return true;
}

database_statistics_s sqlite_database_backend_c::get_statistics() const noexcept
{
  database_statistics_s statistics;
  statistics.accepted = m_accepted.load(std::memory_order_relaxed);
  statistics.rejected = m_rejected.load(std::memory_order_relaxed);
  statistics.dropped = m_queue.get_dropped_count();
  statistics.committed = m_committed.load(std::memory_order_relaxed);
  statistics.failed = m_failed.load(std::memory_order_relaxed);
  return statistics;
}

void sqlite_database_backend_c::shutdown() noexcept
{
  if (!m_running.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  m_queue.shutdown();
  if (m_worker.joinable()) {
    m_worker.join();
  }
}

void sqlite_database_backend_c::worker_loop() noexcept
{
  configure_writer_thread();

  database_record_s record;
  while (m_queue.wait_and_pop(record)) {
    m_batch.emplace_back(std::move(record));
    while (m_batch.size() < m_options.batch_size && m_queue.try_pop(record)) {
      m_batch.emplace_back(std::move(record));
    }
    if (this->insert_batch(m_batch)) {
      m_committed.fetch_add(
        static_cast<common::uint64_t>(m_batch.size()),
        std::memory_order_relaxed);
    } else {
      m_failed.fetch_add(
        static_cast<common::uint64_t>(m_batch.size()),
        std::memory_order_relaxed);
    }
    m_batch.clear();
  }
}

bool sqlite_database_backend_c::insert_batch(
  std::vector<database_record_s> & batch) noexcept
{
  common::uint32_t attempt = 0U;
  while (true) {
    try {
      sqlite_transaction_c transaction(m_connection);
      for (database_record_s & record : batch) {
        const payload_storage_t & payload = *record.payload;
        database_entry_s entry;
        entry.timestamp_ns = record.timestamp_ns;
        entry.sequence = record.sequence;
        entry.source = record.source;
        entry.record_type = record.record_type;
        entry.encoding = record.encoding;
        entry.payload = blob_view_s{payload.data(), payload.size()};
        m_inserter->insert(entry);
      }
      transaction.commit();
      return true;
    } catch (...) {  // NOLINT(bugprone-empty-catch)
      // Retry the bounded transaction until the configured limit is reached.
    }
    if (attempt == m_options.max_write_retries) {
      break;
    }
    ++attempt;
  }
  return false;
}

}  // namespace database
