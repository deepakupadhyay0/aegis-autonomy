#include <gtest/gtest.h>

#include "localization/localization.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{

std::vector<localization::point_3f_s> make_structured_map()
{
  const std::array<localization::translation_3d_s, 8U> centers{{
    {0.25, 0.25, 0.25},
    {2.25, 0.25, 0.35},
    {0.25, 2.25, 0.45},
    {2.25, 2.25, 0.55},
    {0.25, 0.25, 2.25},
    {2.25, 0.25, 2.45},
    {0.25, 2.25, 2.65},
    {2.25, 2.25, 2.85}}};
  constexpr std::array<common::float64_t, 2U> OFFSETS{-0.05, 0.05};

  std::vector<localization::point_3f_s> points;
  points.reserve(centers.size() * 8U);
  for (const localization::translation_3d_s & center : centers) {
    for (const common::float64_t x_offset : OFFSETS) {
      for (const common::float64_t y_offset : OFFSETS) {
        for (const common::float64_t z_offset : OFFSETS) {
          points.push_back(localization::point_3f_s{
              static_cast<common::float32_t>(center.x_m + x_offset),
              static_cast<common::float32_t>(center.y_m + y_offset),
              static_cast<common::float32_t>(center.z_m + z_offset)});
        }
      }
    }
  }
  return points;
}

std::vector<localization::point_3f_s> make_scan(
  const std::vector<localization::point_3f_s> & map_points,
  const common::float64_t translation_x_m,
  const common::float64_t translation_y_m,
  const common::float64_t translation_z_m,
  const common::float64_t yaw_rad)
{
  const common::float64_t cosine = std::cos(yaw_rad);
  const common::float64_t sine = std::sin(yaw_rad);
  std::vector<localization::point_3f_s> scan;
  scan.reserve(map_points.size());

  for (const localization::point_3f_s & point : map_points) {
    const common::float64_t shifted_x =
      static_cast<common::float64_t>(point.x_m) - translation_x_m;
    const common::float64_t shifted_y =
      static_cast<common::float64_t>(point.y_m) - translation_y_m;
    scan.push_back(localization::point_3f_s{
        static_cast<common::float32_t>(
          cosine * shifted_x + sine * shifted_y),
        static_cast<common::float32_t>(
          -sine * shifted_x + cosine * shifted_y),
        static_cast<common::float32_t>(
          static_cast<common::float64_t>(point.z_m) -
          translation_z_m)});
  }
  return scan;
}

common::float64_t yaw_from_quaternion(
  const localization::quaternion_s & quaternion)
{
  const common::float64_t sine =
    2.0 * (quaternion.w * quaternion.z +
    quaternion.x * quaternion.y);
  const common::float64_t cosine =
    1.0 - 2.0 * (quaternion.y * quaternion.y +
    quaternion.z * quaternion.z);
  return std::atan2(sine, cosine);
}

autonomy_config::Localization::Map make_map_config(
  const common::float64_t voxel_size_m = 1.0)
{
  return autonomy_config::Localization::Map(
    voxel_size_m,
    6,
    1.0e-3,
    1.0e-2,
    1'024,
    256);
}

autonomy_config::Localization::Localizer make_localizer_config(
  const common::int64_t maximum_scan_points = 1'024,
  const common::int64_t minimum_correspondences = 20,
  const common::float64_t maximum_mahalanobis_distance_squared = 25.0,
  const common::float64_t translation_convergence_m = 1.0e-4,
  const common::float64_t rotation_convergence_rad = 1.0e-4)
{
  return autonomy_config::Localization::Localizer(
    30,
    6,
    1,
    maximum_scan_points,
    minimum_correspondences,
    maximum_mahalanobis_distance_squared,
    1.0e-6,
    translation_convergence_m,
    rotation_convergence_rad,
    1.0,
    0.35);
}

}  // namespace

TEST(NdtMapTest, BuildsRegularizedVoxelDistributions)
{
  const std::vector<localization::point_3f_s> points =
    make_structured_map();
  const autonomy_config::Localization::Map config = make_map_config();
  localization::ndt_map_c map{config};

  EXPECT_EQ(map.rebuild(points), localization::status_e::success);
  EXPECT_TRUE(map.ready());
  EXPECT_EQ(map.source_point_count(), points.size());
  EXPECT_EQ(map.voxel_count(), 8U);
}

TEST(NdtMapTest, RejectsNonFiniteInputWithoutReplacingMap)
{
  std::vector<localization::point_3f_s> points =
    make_structured_map();
  localization::ndt_map_c map{make_map_config()};
  ASSERT_EQ(map.rebuild(points), localization::status_e::success);
  const std::size_t previous_voxel_count = map.voxel_count();

  points[0U].x_m =
    std::numeric_limits<common::float32_t>::quiet_NaN();
  EXPECT_EQ(
    map.rebuild(points),
    localization::status_e::invalid_point_cloud);
  EXPECT_TRUE(map.ready());
  EXPECT_EQ(map.voxel_count(), previous_voxel_count);
}

TEST(NdtMapTest, RejectsInvalidConfiguration)
{
  const autonomy_config::Localization::Map config =
    make_map_config(0.0);
  EXPECT_THROW(
    localization::ndt_map_c{config},
    std::invalid_argument);
}

TEST(NdtLocalizerTest, RecoversControlledRigidTransform)
{
  constexpr common::float64_t TRANSLATION_X_M = 0.12;
  constexpr common::float64_t TRANSLATION_Y_M = -0.08;
  constexpr common::float64_t TRANSLATION_Z_M = 0.04;
  constexpr common::float64_t YAW_RAD = 0.03;

  const std::vector<localization::point_3f_s> map_points =
    make_structured_map();
  const std::vector<localization::point_3f_s> scan = make_scan(
    map_points,
    TRANSLATION_X_M,
    TRANSLATION_Y_M,
    TRANSLATION_Z_M,
    YAW_RAD);

  localization::ndt_map_c map{make_map_config()};
  ASSERT_EQ(map.rebuild(map_points), localization::status_e::success);

  const autonomy_config::Localization::Localizer config =
    make_localizer_config(1'024, 32, 100.0, 1.0e-6, 1.0e-6);
  localization::ndt_localizer_c localizer{config};

  localization::localization_result_s result{};
  const localization::status_e status = localizer.localize(
    map,
    scan,
    localization::pose_3d_s{},
    result);

  EXPECT_EQ(status, localization::status_e::success);
  EXPECT_TRUE(result.converged);
  EXPECT_GE(result.correspondence_count,
    static_cast<std::size_t>(config.get_minimum_correspondences()));
  EXPECT_NEAR(
    result.pose_map_from_scan.translation.x_m,
    TRANSLATION_X_M,
    2.0e-3);
  EXPECT_NEAR(
    result.pose_map_from_scan.translation.y_m,
    TRANSLATION_Y_M,
    2.0e-3);
  EXPECT_NEAR(
    result.pose_map_from_scan.translation.z_m,
    TRANSLATION_Z_M,
    2.0e-3);
  EXPECT_NEAR(
    yaw_from_quaternion(result.pose_map_from_scan.rotation),
    YAW_RAD,
    2.0e-3);
}

TEST(CudaNdtLocalizerTest, MatchesCpuReferenceOnControlledTransform)
{
  constexpr common::float64_t TRANSLATION_X_M = 0.12;
  constexpr common::float64_t TRANSLATION_Y_M = -0.08;
  constexpr common::float64_t TRANSLATION_Z_M = 0.04;
  constexpr common::float64_t YAW_RAD = 0.03;

  const std::vector<localization::point_3f_s> map_points =
    make_structured_map();
  const std::vector<localization::point_3f_s> scan = make_scan(
    map_points,
    TRANSLATION_X_M,
    TRANSLATION_Y_M,
    TRANSLATION_Z_M,
    YAW_RAD);
  localization::ndt_map_c map{make_map_config()};
  ASSERT_EQ(map.rebuild(map_points), localization::status_e::success);

  const autonomy_config::Localization::Localizer config =
    make_localizer_config(1'024, 32, 100.0, 1.0e-6, 1.0e-6);

  localization::ndt_localizer_c cpu_localizer{config};
  localization::localization_result_s cpu_result{};
  ASSERT_EQ(
    cpu_localizer.localize(
      map, scan, localization::pose_3d_s{}, cpu_result),
    localization::status_e::success);

  const autonomy_config::Localization::Accelerator & accelerator_config =
    autonomy_config::Localization::get_compile_time_values().get_accelerator();
  std::unique_ptr<localization::cuda_ndt_localizer_c> cuda_localizer;
  try {
    cuda_localizer =
      std::make_unique<localization::cuda_ndt_localizer_c>(
        map,
        config,
        accelerator_config);
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  localization::localization_result_s cuda_result{};
  EXPECT_EQ(
    cuda_localizer->localize(
      scan, localization::pose_3d_s{}, cuda_result),
    localization::status_e::success);
  EXPECT_TRUE(cuda_result.converged);
  EXPECT_EQ(cuda_result.correspondence_count, cpu_result.correspondence_count);
  EXPECT_NEAR(
    cuda_result.pose_map_from_scan.translation.x_m,
    cpu_result.pose_map_from_scan.translation.x_m,
    1.0e-6);
  EXPECT_NEAR(
    cuda_result.pose_map_from_scan.translation.y_m,
    cpu_result.pose_map_from_scan.translation.y_m,
    1.0e-6);
  EXPECT_NEAR(
    cuda_result.pose_map_from_scan.translation.z_m,
    cpu_result.pose_map_from_scan.translation.z_m,
    1.0e-6);
  EXPECT_NEAR(
    yaw_from_quaternion(cuda_result.pose_map_from_scan.rotation),
    yaw_from_quaternion(cpu_result.pose_map_from_scan.rotation),
    1.0e-6);
}

TEST(NdtLocalizerTest, RejectsUnboundedAndInvalidScans)
{
  const std::vector<localization::point_3f_s> map_points =
    make_structured_map();
  localization::ndt_map_c map{make_map_config()};
  ASSERT_EQ(map.rebuild(map_points), localization::status_e::success);

  const autonomy_config::Localization::Localizer config =
    make_localizer_config(32, 6);
  localization::ndt_localizer_c localizer{config};

  localization::localization_result_s result{};
  EXPECT_EQ(
    localizer.localize(
      map, map_points, localization::pose_3d_s{}, result),
    localization::status_e::capacity_exceeded);

  const std::array<localization::point_3f_s, 1U> invalid_scan{{
    {std::numeric_limits<common::float32_t>::infinity(), 0.0F, 0.0F}}};
  EXPECT_EQ(
    localizer.localize(
      map, invalid_scan, localization::pose_3d_s{}, result),
    localization::status_e::invalid_point_cloud);
}
