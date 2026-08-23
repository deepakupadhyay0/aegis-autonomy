#include <gtest/gtest.h>
#include "autonomy_config/ai_diagnostics.hpp"
#include "autonomy_config/database.hpp"
#include "autonomy_config/logging.hpp"
#include "autonomy_config/presence_detection.hpp"

TEST(GeneratedConfigTest, RuntimeValuesRetrieval)
{
  EXPECT_NO_THROW({
    auto const config = autonomy_config::PresenceDetection::get_run_time_values();
    (void)config;
  });
}

TEST(GeneratedConfigTest, CameraSettingsVerification)
{
  auto const config = autonomy_config::PresenceDetection::get_run_time_values();

  EXPECT_GE(config.get_camera().get_device_id(), 0);
  EXPECT_GT(config.get_camera().get_image_width(), 0);
  EXPECT_GT(config.get_camera().get_image_height(), 0);
  EXPECT_GT(config.get_camera().get_fps(), 0.0);
}

TEST(GeneratedConfigTest, PerceptionSettingsVerification)
{
  auto const config = autonomy_config::PresenceDetection::get_run_time_values();

  const common::float64_t confidence =
    config.get_perception().get_confidence_threshold();
  const common::float64_t nms =
    config.get_perception().get_nms_threshold();

  EXPECT_GE(confidence, 0.0);
  EXPECT_LE(confidence, 1.0);

  EXPECT_GE(nms, 0.0);
  EXPECT_LE(nms, 1.0);
}

TEST(GeneratedConfigTest, LoggingDefaultsVerification)
{
  const autonomy_config::Logging & logging =
    autonomy_config::Logging::get_compile_time_values();

  EXPECT_FALSE(logging.get_log_directory().empty());
  EXPECT_GT(logging.get_queue_capacity(), 0);
  EXPECT_GT(logging.get_file_size_bytes(), 0);
  EXPECT_GT(logging.get_max_files(), 0);
  EXPECT_TRUE(logging.get_diagnostics_enabled());
  EXPECT_GT(logging.get_diagnostics_interval_ms(), 0);
}

TEST(GeneratedConfigTest, DatabaseDefaultsVerification)
{
  const autonomy_config::Database & database =
    autonomy_config::Database::get_compile_time_values();

  EXPECT_FALSE(database.get_database_path().empty());
  EXPECT_GT(database.get_queue_capacity(), 0);
  EXPECT_GT(database.get_batch_size(), 0);
  EXPECT_LE(database.get_batch_size(), database.get_queue_capacity());
  EXPECT_GT(database.get_busy_timeout_ms(), 0);
  EXPECT_GE(database.get_max_write_retries(), 0);
}

TEST(GeneratedConfigTest, AiDiagnosticsDefaultsVerification)
{
  const autonomy_config::AiDiagnostics & diagnostics =
    autonomy_config::AiDiagnostics::get_compile_time_values();

  EXPECT_FALSE(diagnostics.get_enabled());
  EXPECT_EQ(diagnostics.get_input_mode(), "dds");
  EXPECT_FALSE(diagnostics.get_input_topic().empty());
  EXPECT_FALSE(diagnostics.get_report_topic().empty());
  EXPECT_GT(diagnostics.get_queue_capacity(), 0);
  EXPECT_FALSE(diagnostics.get_minimum_log_level().empty());
  EXPECT_GT(diagnostics.get_max_records_per_batch(), 0);
  EXPECT_GT(diagnostics.get_maximum_context_bytes(), 0);
  EXPECT_FALSE(diagnostics.get_cursor_filename().empty());
  EXPECT_GE(diagnostics.get_cpu_threshold_percent(), 0.0);
  EXPECT_LE(diagnostics.get_cpu_threshold_percent(), 100.0);
  EXPECT_GT(diagnostics.get_idle_samples_required(), 0);
  EXPECT_GT(diagnostics.get_request_timeout_ms(), 0);
  EXPECT_GT(diagnostics.get_maximum_response_bytes(), 0);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
