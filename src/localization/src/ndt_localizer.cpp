#include "localization/ndt_localizer.hpp"

#include "ndt_solver.hpp"

#include "common/config_validation.hpp"

#include <Eigen/Core>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace localization
{

namespace
{

bool finite_point(const point_3f_s & point) noexcept
{
  return std::isfinite(point.x_m) &&
         std::isfinite(point.y_m) &&
         std::isfinite(point.z_m);
}

void require_positive_finite(
  const common::float64_t value,
  const char * const error_message)
{
  if (!std::isfinite(value) || value <= 0.0) {
    throw std::invalid_argument(error_message);
  }
}

}  // namespace

void validate_ndt_localizer_config(
  const autonomy_config::Localization::Localizer & config)
{
  common::validation::require_positive_representable<common::uint32_t>(
    config.get_maximum_iterations(),
    "localizer.maximum_iterations must fit in uint32 and be positive");

  common::validation::require_in_closed_range(
    config.get_maximum_line_search_steps(), 1, 12,
    "localizer.maximum_line_search_steps must be in [1, 12]");

  common::validation::require_in_closed_range(
    config.get_neighbor_radius_voxels(), 0, 2,
    "localizer.neighbor_radius_voxels must be in [0, 2]");

  const common::int64_t maximum_scan_points = config.get_maximum_scan_points();
  common::validation::require_positive_representable<std::size_t>(
    maximum_scan_points,
    "localizer.maximum_scan_points must fit in size_t and be positive");

  common::validation::require_in_closed_range(
    config.get_minimum_correspondences(), 6, maximum_scan_points,
    "localizer.minimum_correspondences must be at least 6 and no "
    "greater than maximum_scan_points");

  require_positive_finite(
    config.get_maximum_mahalanobis_distance_squared(),
    "localizer.maximum_mahalanobis_distance_squared must be finite and positive");
  require_positive_finite(
    config.get_hessian_damping(),
    "localizer.hessian_damping must be finite and positive");
  require_positive_finite(
    config.get_translation_convergence_m(),
    "localizer.translation_convergence_m must be finite and positive");
  require_positive_finite(
    config.get_rotation_convergence_rad(),
    "localizer.rotation_convergence_rad must be finite and positive");
  require_positive_finite(
    config.get_maximum_translation_step_m(),
    "localizer.maximum_translation_step_m must be finite and positive");
  require_positive_finite(
    config.get_maximum_rotation_step_rad(),
    "localizer.maximum_rotation_step_rad must be finite and positive");
}

ndt_localizer_c::ndt_localizer_c(
  const autonomy_config::Localization::Localizer & config)
: m_config(config)
{
  validate_ndt_localizer_config(config);
}

status_e ndt_localizer_c::localize(
  const ndt_map_c & map,
  const std::span<const point_3f_s> scan,
  const pose_3d_s & initial_pose_map_from_scan,
  localization_result_s & result) const noexcept
{
  result = localization_result_s{};
  if (!map.ready()) {
    return status_e::map_not_ready;
  }
  if (scan.empty()) {
    return status_e::invalid_point_cloud;
  }
  if (scan.size() > static_cast<std::size_t>(m_config.get_maximum_scan_points())) {
    return status_e::capacity_exceeded;
  }
  for (const point_3f_s & point : scan) {
    if (!finite_point(point)) {
      return status_e::invalid_point_cloud;
    }
  }

  const auto evaluate =
    [&map, &scan, this](
    const ndt_solver::pose_state_s & pose,
    const bool calculate_derivatives,
    ndt_solver::normal_equation_s & equation) noexcept
    {
      equation = ndt_solver::normal_equation_s{};
      for (const point_3f_s & point : scan) {
        const ndt_solver::vector3_t point_scan{
          static_cast<common::float64_t>(point.x_m),
          static_cast<common::float64_t>(point.y_m),
          static_cast<common::float64_t>(point.z_m)};
        const ndt_solver::vector3_t point_map =
          pose.rotation * point_scan + pose.translation;
        const std::array<common::float64_t, 3U> point_values{
          point_map.x(), point_map.y(), point_map.z()};
        const ndt_map_c::distribution_s * distribution =
          map.best_distribution(
          point_values,
          static_cast<common::uint32_t>(
            m_config.get_neighbor_radius_voxels()));
        if (distribution == nullptr) {
          continue;
        }

        const Eigen::Map<const ndt_solver::vector3_t> mean{
          distribution->mean_m.data()};
        const Eigen::Map<const ndt_solver::matrix3_t> information{
          distribution->information.data()};
        const ndt_solver::vector3_t residual = point_map - mean;
        const ndt_solver::vector3_t weighted_residual =
          information * residual;
        const common::float64_t squared_distance =
          residual.dot(weighted_residual);
        if (!std::isfinite(squared_distance) ||
          squared_distance > m_config.get_maximum_mahalanobis_distance_squared())
        {
          continue;
        }

        equation.cost += 0.5 * std::max(0.0, squared_distance);
        ++equation.correspondence_count;
        if (!calculate_derivatives) {
          continue;
        }

        Eigen::Matrix<common::float64_t, 3, 6> jacobian =
          Eigen::Matrix<common::float64_t, 3, 6>::Zero();
        jacobian.template leftCols<3>() = ndt_solver::matrix3_t::Identity();
        jacobian.template rightCols<3>() =
          -ndt_solver::skew_symmetric(point_map);
        equation.hessian +=
          jacobian.transpose() * information * jacobian;
        equation.gradient += jacobian.transpose() * weighted_residual;
      }
      return status_e::success;
    };

  return ndt_solver::solve(
    m_config, initial_pose_map_from_scan, result, evaluate);
}

}  // namespace localization
