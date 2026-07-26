#include <gtest/gtest.h>
#include "autonomy_config/autonomy_settings.hpp"

TEST(AutonomyConfigTest, RuntimeValuesRetrieval)
{
  EXPECT_NO_THROW({
    auto const config = autonomy_config::AutonomySettings::get_run_time_values();
    (void)config;
  });
}

TEST(AutonomyConfigTest, CameraSettingsVerification)
{
  auto const config = autonomy_config::AutonomySettings::get_run_time_values();

  EXPECT_GE(config.get_camera().get_device_id(), 0);
  EXPECT_GT(config.get_camera().get_image_width(), 0);
  EXPECT_GT(config.get_camera().get_image_height(), 0);
  EXPECT_GT(config.get_camera().get_fps(), 0.0);
}

TEST(AutonomyConfigTest, PerceptionSettingsVerification)
{
  auto const config = autonomy_config::AutonomySettings::get_run_time_values();

  double conf = config.get_perception().get_confidence_threshold();
  double nms = config.get_perception().get_nms_threshold();

  EXPECT_GE(conf, 0.0);
  EXPECT_LE(conf, 1.0);

  EXPECT_GE(nms, 0.0);
  EXPECT_LE(nms, 1.0);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
