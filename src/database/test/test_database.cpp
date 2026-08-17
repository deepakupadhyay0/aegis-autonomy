#include "database/database_backend.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace
{

class database_file_guard_c final
{
public:
  database_file_guard_c()
  : m_path(
      std::filesystem::temp_directory_path() /
      ("aegis_database_test_" +
      std::to_string(static_cast<common::int64_t>(::getpid())) + ".db"))
  {
    this->remove_files();
  }

  ~database_file_guard_c() noexcept
  {
    this->remove_files();
  }

  database_file_guard_c(const database_file_guard_c &) = delete;
  database_file_guard_c & operator=(const database_file_guard_c &) = delete;
  database_file_guard_c(database_file_guard_c &&) = delete;
  database_file_guard_c & operator=(database_file_guard_c &&) = delete;

  const std::filesystem::path & get_path() const noexcept
  {
    return m_path;
  }

private:
  void remove_files() const noexcept
  {
    std::error_code filesystem_error;
    static_cast<void>(std::filesystem::remove(m_path, filesystem_error));
    static_cast<void>(std::filesystem::remove(
        m_path.string() + "-wal", filesystem_error));
    static_cast<void>(std::filesystem::remove(
        m_path.string() + "-shm", filesystem_error));
  }

  std::filesystem::path m_path;
};

}  // namespace

TEST(DatabaseBackendTest, StoresBinaryRecordAsynchronously)
{
  const database_file_guard_c database_files;

  database::database_options_s options;
  options.database_path.assign(database_files.get_path().string());
  options.queue_capacity = 8U;
  options.batch_size = 4U;
  options.busy_timeout_ms = 10U;
  options.max_write_retries = 1U;
  database::sqlite_database_backend_c backend(options);

  const std::shared_ptr<const database::payload_storage_t> payload =
    std::make_shared<const database::payload_storage_t>(
    database::payload_storage_t{std::byte{0x00}, std::byte{0xFF}});

  database::database_record_s record;
  record.timestamp_ns = 1;
  record.sequence = 2U;
  record.source = "test_node";
  record.record_type = "raw_test";
  record.encoding = "binary";
  record.payload = payload;
  ASSERT_TRUE(backend.try_store(std::move(record)));

  database::database_record_s empty_record;
  empty_record.timestamp_ns = 2;
  empty_record.sequence = 3U;
  empty_record.source = "test_node";
  empty_record.record_type = "empty_test";
  empty_record.encoding = "binary";
  empty_record.payload =
    std::make_shared<const database::payload_storage_t>();
  ASSERT_TRUE(backend.try_store(std::move(empty_record)));

  backend.shutdown();
  const database::database_statistics_s statistics = backend.get_statistics();
  EXPECT_EQ(statistics.accepted, 2U);
  EXPECT_EQ(statistics.rejected, 0U);
  EXPECT_EQ(statistics.committed, 2U);
  EXPECT_EQ(statistics.failed, 0U);
}
