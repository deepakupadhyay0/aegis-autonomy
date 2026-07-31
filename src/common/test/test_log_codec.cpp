#include "common/logging/log_codec.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

TEST(LogCodecTest, RoundTripsRegistration)
{
  common::logging::log_registration_s registration;
  registration.node_name = "camera_node";
  registration.enable_console_log = true;

  std::array<
    std::byte,
    common::logging::LOG_REGISTRATION_WIRE_SIZE> buffer{};
  common::logging::serialize_log_registration(registration, buffer);

  common::logging::log_registration_s decoded;
  ASSERT_TRUE(common::logging::deserialize_log_registration(
      buffer.data(), buffer.size(), decoded));
  EXPECT_EQ(decoded.node_name, registration.node_name);
  EXPECT_EQ(decoded.enable_console_log, registration.enable_console_log);
}

TEST(LogCodecTest, RoundTripsRecord)
{
  common::logging::log_record_s record;
  record.timestamp_ns = 123456789U;
  record.dropped_record_count = 3U;
  record.source_line = 42U;
  record.level = common::logging::log_level_e::warning;
  record.thread_name = "camera_main";
  record.source_file = "camera_node.cpp";
  record.message = "Frame dropped";

  std::array<std::byte, common::logging::LOG_RECORD_WIRE_SIZE> buffer{};
  common::logging::serialize_log_record(record, buffer);

  common::logging::log_record_s decoded;
  ASSERT_TRUE(common::logging::deserialize_log_record(
      buffer.data(), buffer.size(), decoded));
  EXPECT_EQ(decoded.timestamp_ns, record.timestamp_ns);
  EXPECT_EQ(decoded.dropped_record_count, record.dropped_record_count);
  EXPECT_EQ(decoded.source_line, record.source_line);
  EXPECT_EQ(decoded.level, record.level);
  EXPECT_EQ(decoded.thread_name, record.thread_name);
  EXPECT_EQ(decoded.source_file, record.source_file);
  EXPECT_EQ(decoded.message, record.message);
}
