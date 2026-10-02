#include <gtest/gtest.h>
#include "autonomy_config/ai_diagnostics.hpp"
#include "autonomy_config/database.hpp"
#include "autonomy_config/logging.hpp"
#include "autonomy_config/localization.hpp"
#include "autonomy_config/place_recognition.hpp"
#include "autonomy_config/presence_detection.hpp"
#include "autonomy_config/topic_timing.hpp"

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

TEST(GeneratedConfigTest, TopicTimingDefaultsVerification)
{
  const autonomy_config::TopicTiming & timing =
    autonomy_config::TopicTiming::get_compile_time_values();

  ASSERT_EQ(timing.get_topic_names().size(), timing.get_expected_rates_hz().size());
  EXPECT_FALSE(timing.get_topic_names().empty());
  EXPECT_GT(timing.get_window_sample_count(), timing.get_startup_sample_count());
  EXPECT_GT(timing.get_error_timeout_ms(), timing.get_warning_timeout_ms());
}

TEST(GeneratedConfigTest, PlaceRecognitionDefaultsVerification)
{
  const autonomy_config::PlaceRecognition & place_recognition =
    autonomy_config::PlaceRecognition::get_compile_time_values();

  EXPECT_FALSE(place_recognition.get_ros().get_image_topic().empty());
  EXPECT_FALSE(place_recognition.get_ros().get_descriptor_topic().empty());
  EXPECT_GT(place_recognition.get_ros().get_image_queue_depth(), 0);
  EXPECT_GT(place_recognition.get_model().get_wifi_feature_count(), 0);
  EXPECT_GT(place_recognition.get_model().get_access_point_count(), 0);
  EXPECT_GT(place_recognition.get_model().get_descriptor_size(), 0);
  EXPECT_GT(place_recognition.get_model().get_image_width(), 0);
  EXPECT_GT(place_recognition.get_model().get_image_height(), 0);
}

TEST(GeneratedConfigTest, LocalizationDefaultsVerification)
{
  const autonomy_config::Localization & localization =
    autonomy_config::Localization::get_compile_time_values();

  EXPECT_FALSE(localization.get_ros().get_points_topic().empty());
  EXPECT_FALSE(localization.get_ros().get_pose_topic().empty());
  EXPECT_FALSE(localization.get_ros().get_map_frame().empty());
  EXPECT_GT(localization.get_ros().get_pose_queue_depth(), 0);
  EXPECT_GT(localization.get_map().get_voxel_size_m(), 0.0);
  EXPECT_GE(localization.get_map().get_minimum_points_per_voxel(), 4);
  EXPECT_GT(localization.get_map().get_maximum_points(), 0);
  EXPECT_GT(localization.get_map().get_maximum_voxels(), 0);
  EXPECT_GT(localization.get_localizer().get_maximum_scan_points(), 0);
  EXPECT_GE(localization.get_localizer().get_minimum_correspondences(), 6);
  EXPECT_GE(localization.get_accelerator().get_device_index(), 0);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
