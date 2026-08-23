#include "ai_diagnostics/log_reader.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace
{

class temporary_directory_c final
{
public:
  temporary_directory_c()
  {
    char path_template[] = "/tmp/aegis_log_reader_XXXXXX";
    char * const created_path = ::mkdtemp(path_template);
    if (created_path == nullptr) {
      throw std::runtime_error("Failed to create temporary test directory");
    }
    m_path = created_path;
  }

  ~temporary_directory_c() noexcept
  {
    std::error_code error;
    std::filesystem::remove_all(m_path, error);
  }

  temporary_directory_c(const temporary_directory_c &) = delete;
  temporary_directory_c & operator=(const temporary_directory_c &) = delete;
  temporary_directory_c(temporary_directory_c &&) = delete;
  temporary_directory_c & operator=(temporary_directory_c &&) = delete;

  const std::filesystem::path & path() const noexcept
  {
    return m_path;
  }

private:
  std::filesystem::path m_path;
};

ai_diagnostics::ai_diagnostics_options_s make_options(
  const std::filesystem::path & log_directory)
{
  ai_diagnostics::ai_diagnostics_options_s options;
  options.log_directory.assign(log_directory.string());
  options.cursor_filename = ".cursor";
  options.minimum_log_level = logging::log_level_e::info;
  options.max_log_files = 3U;
  options.max_records_per_batch = 8U;
  options.maximum_context_bytes = 4096U;
  return options;
}

}  // namespace

TEST(LogReaderTest, ParsesStructuredLogLine)
{
  ai_diagnostics::parsed_log_record_s record;
  ASSERT_TRUE(ai_diagnostics::parse_log_line(
      "camera_node",
      "camera_node:1:0",
      "2026-08-21T12:34:56.123456789Z: camera: WARN: "
      "camera_node.cpp:42: marker distance changed: expected 4.2 m",
      record));

  EXPECT_EQ(record.node_name, "camera_node");
  EXPECT_EQ(record.thread_name, "camera");
  EXPECT_EQ(record.level, logging::log_level_e::warning);
  EXPECT_EQ(record.source_file, "camera_node.cpp");
  EXPECT_EQ(record.source_line, 42U);
  EXPECT_EQ(record.message, "marker distance changed: expected 4.2 m");
}

TEST(LogReaderTest, FiltersLevelAndPersistsCursor)
{
  const temporary_directory_c directory;
  const std::filesystem::path log_path =
    directory.path() / "camera_node.1.log";
  {
    std::ofstream log(log_path);
    log << "2026-08-21T12:00:00.000000000Z: camera: DEBUG: "
      "camera.cpp:10: ignored debug record\n";
    log << "2026-08-21T12:00:01.000000000Z: camera: INFO: "
      "camera.cpp:11: accepted info record\n";
  }

  const ai_diagnostics::ai_diagnostics_options_s options =
    make_options(directory.path());
  {
    ai_diagnostics::log_reader_c reader(options);
    const std::vector<ai_diagnostics::parsed_log_record_s> records =
      reader.read_next_batch();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().node_name, "camera_node");
    EXPECT_EQ(records.front().message, "accepted info record");
  }
  {
    ai_diagnostics::log_reader_c reader(options);
    EXPECT_TRUE(reader.read_next_batch().empty());
  }
}

TEST(LogReaderTest, WaitsForIncompleteFinalLine)
{
  const temporary_directory_c directory;
  const std::filesystem::path log_path = directory.path() / "camera_node.log";
  {
    std::ofstream log(log_path);
    log << "2026-08-21T12:00:00.000000000Z: camera: WARN: "
      "camera.cpp:12: incomplete warning";
  }

  ai_diagnostics::log_reader_c reader(make_options(directory.path()));
  EXPECT_TRUE(reader.read_next_batch().empty());

  {
    std::ofstream log(log_path, std::ios::app);
    log << '\n';
  }
  const std::vector<ai_diagnostics::parsed_log_record_s> records =
    reader.read_next_batch();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records.front().message, "incomplete warning");
}
