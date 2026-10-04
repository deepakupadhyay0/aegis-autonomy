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

TEST(LogReaderTest, ExtractsBoundedNumericMeasurements)
{
  ai_diagnostics::diagnostic_event_s event;
  event.fault =
    "health=ok pose_innovation_m=0.02; lidar_odom_offset_ms=87 "
    "status=ready invalid=nan";
  ai_diagnostics::extract_numeric_log_measurements(event);
  ASSERT_EQ(event.measurements.size(), 2U);
  EXPECT_EQ(event.measurements[0].name, "pose_innovation_m");
  EXPECT_EQ(event.measurements[0].value, "0.02");
  EXPECT_EQ(event.measurements[1].name, "lidar_odom_offset_ms");
  EXPECT_EQ(event.measurements[1].value, "87");
  EXPECT_FALSE(event.truncated);

  event.fault =
    "a=1 b=2 c=3 d=4 e=5 f=6 g=7 h=8 i=9";
  ai_diagnostics::extract_numeric_log_measurements(event);
  EXPECT_EQ(event.measurements.size(),
    ai_diagnostics::MAX_DIAGNOSTIC_MEASUREMENTS);
  EXPECT_TRUE(event.truncated);
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
    EXPECT_TRUE(reader.commit());
  }
  {
    ai_diagnostics::log_reader_c reader(options);
    EXPECT_TRUE(reader.read_next_batch().empty());
  }
}

TEST(LogReaderTest, RetainsExplicitHealthyStatusBelowMinimumLevel)
{
  const temporary_directory_c directory;
  const std::filesystem::path log_path =
    directory.path() / "ndt_localization_node.log";
  {
    std::ofstream log(log_path);
    log << "2026-08-21T12:00:00.000000000Z: ndt: INFO: "
      "ndt.cpp:10: routine message\n";
    log << "2026-08-21T12:00:01.000000000Z: ndt: INFO: "
      "ndt.cpp:11: health=ok pose_innovation_m=0.02\n";
  }

  ai_diagnostics::ai_diagnostics_options_s options =
    make_options(directory.path());
  options.minimum_log_level = logging::log_level_e::warning;
  ai_diagnostics::log_reader_c reader(options);
  const std::vector<ai_diagnostics::parsed_log_record_s> records =
    reader.read_next_batch();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records.front().message,
    "health=ok pose_innovation_m=0.02");
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

TEST(LogReaderTest, ReplaysUncommittedBatchAfterRestart)
{
  const temporary_directory_c directory;
  const std::filesystem::path log_path = directory.path() / "lidar_node.log";
  {
    std::ofstream log(log_path);
    log << "2026-08-21T12:00:00.000000000Z: worker: ERROR: "
      "lidar.cpp:21: CUDA evaluation failed\n";
  }

  const ai_diagnostics::ai_diagnostics_options_s options =
    make_options(directory.path());
  {
    ai_diagnostics::log_reader_c reader(options);
    ASSERT_EQ(reader.read_next_batch().size(), 1U);
  }
  {
    ai_diagnostics::log_reader_c reader(options);
    const std::vector<ai_diagnostics::parsed_log_record_s> records =
      reader.read_next_batch();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().message, "CUDA evaluation failed");
    EXPECT_TRUE(reader.commit());
  }
  {
    ai_diagnostics::log_reader_c reader(options);
    EXPECT_TRUE(reader.read_next_batch().empty());
  }
}

TEST(LogReaderTest, SkipsOversizedLineAndContinues)
{
  const temporary_directory_c directory;
  const std::filesystem::path log_path = directory.path() / "camera_node.log";
  {
    std::ofstream log(log_path);
    log << "2026-08-21T12:00:00.000000000Z: camera: WARN: "
      "camera.cpp:12: " << std::string(5000U, 'x') << '\n';
    log << "2026-08-21T12:00:01.000000000Z: camera: ERROR: "
      "camera.cpp:13: useful warning\n";
  }

  ai_diagnostics::log_reader_c reader(make_options(directory.path()));
  const std::vector<ai_diagnostics::parsed_log_record_s> records =
    reader.read_next_batch();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records.front().message, "useful warning");
  EXPECT_TRUE(reader.commit());
  EXPECT_TRUE(reader.read_next_batch().empty());
}
