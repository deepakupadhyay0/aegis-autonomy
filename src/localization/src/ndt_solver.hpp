#pragma once

#include "localization/ndt_localizer.hpp"

#include <Eigen/Cholesky>
#include <Eigen/Core>
#include <Eigen/Geometry>

#include <cmath>
#include <cstddef>

namespace localization::ndt_solver
{

using vector3_t = Eigen::Matrix<common::float64_t, 3, 1>;
using matrix3_t = Eigen::Matrix<common::float64_t, 3, 3>;
using vector6_t = Eigen::Matrix<common::float64_t, 6, 1>;
using matrix6_t = Eigen::Matrix<common::float64_t, 6, 6>;

struct pose_state_s
{
  matrix3_t rotation{matrix3_t::Identity()};
  vector3_t translation{vector3_t::Zero()};
};

struct normal_equation_s
{
  matrix6_t hessian{matrix6_t::Zero()};
  vector6_t gradient{vector6_t::Zero()};
  common::float64_t cost{0.0};
  std::size_t correspondence_count{0U};
};

inline matrix3_t skew_symmetric(const vector3_t & vector) noexcept
{
  matrix3_t skew;
  skew <<
    0.0, -vector.z(), vector.y(),
    vector.z(), 0.0, -vector.x(),
    -vector.y(), vector.x(), 0.0;
  return skew;
}

inline bool try_pose_state(
  const pose_3d_s & pose,
  pose_state_s & state) noexcept
{
  const quaternion_s & rotation = pose.rotation;
  if (!std::isfinite(pose.translation.x_m) ||
    !std::isfinite(pose.translation.y_m) ||
    !std::isfinite(pose.translation.z_m) ||
    !std::isfinite(rotation.x) ||
    !std::isfinite(rotation.y) ||
    !std::isfinite(rotation.z) ||
    !std::isfinite(rotation.w))
  {
    return false;
  }

  Eigen::Quaterniond quaternion{
    rotation.w, rotation.x, rotation.y, rotation.z};
  const common::float64_t norm = quaternion.norm();
  if (!std::isfinite(norm) || norm <= 1.0e-12) {
    return false;
  }
  quaternion.normalize();
  state.rotation = quaternion.toRotationMatrix();
  state.translation = vector3_t{
    pose.translation.x_m,
    pose.translation.y_m,
    pose.translation.z_m};
  return true;
}

inline pose_3d_s to_pose(const pose_state_s & state) noexcept
{
  Eigen::Quaterniond quaternion{state.rotation};
  quaternion.normalize();
  return pose_3d_s{
    translation_3d_s{
      state.translation.x(),
      state.translation.y(),
      state.translation.z()},
    quaternion_s{
      quaternion.x(),
      quaternion.y(),
      quaternion.z(),
      quaternion.w()}};
}

inline pose_state_s apply_increment(
  const pose_state_s & pose,
  const vector6_t & increment) noexcept
{
  const vector3_t translation_increment = increment.template head<3>();
  const vector3_t rotation_increment = increment.template tail<3>();
  const common::float64_t rotation_angle = rotation_increment.norm();

  matrix3_t incremental_rotation = matrix3_t::Identity();
  if (rotation_angle > 1.0e-15) {
    const vector3_t rotation_axis = rotation_increment / rotation_angle;
    incremental_rotation =
      Eigen::AngleAxisd(rotation_angle, rotation_axis).toRotationMatrix();
  }

  return pose_state_s{
    incremental_rotation * pose.rotation,
    incremental_rotation * pose.translation + translation_increment};
}

inline void limit_increment(
  vector6_t & increment,
  const autonomy_config::Localization::Localizer & config) noexcept
{
  vector3_t translation_increment = increment.template head<3>();
  const common::float64_t translation_norm = translation_increment.norm();
  if (translation_norm > config.get_maximum_translation_step_m()) {
    translation_increment *= config.get_maximum_translation_step_m() / translation_norm;
    increment.template head<3>() = translation_increment;
  }

  vector3_t rotation_increment = increment.template tail<3>();
  const common::float64_t rotation_norm = rotation_increment.norm();
  if (rotation_norm > config.get_maximum_rotation_step_rad()) {
    rotation_increment *= config.get_maximum_rotation_step_rad() / rotation_norm;
    increment.template tail<3>() = rotation_increment;
  }
}

template<typename evaluator_t>
status_e solve(
  const autonomy_config::Localization::Localizer & config,
  const pose_3d_s & initial_pose_map_from_scan,
  localization_result_s & result,
  evaluator_t & evaluate) noexcept
{
  const common::uint32_t maximum_iterations =
    static_cast<common::uint32_t>(config.get_maximum_iterations());
  const common::uint32_t maximum_line_search_steps =
    static_cast<common::uint32_t>(config.get_maximum_line_search_steps());
  const std::size_t minimum_correspondences =
    static_cast<std::size_t>(config.get_minimum_correspondences());

  pose_state_s current_pose{};
  if (!try_pose_state(initial_pose_map_from_scan, current_pose)) {
    return status_e::invalid_initial_pose;
  }
  result.pose_map_from_scan = to_pose(current_pose);

  for (common::uint32_t iteration = 0U;
    iteration < maximum_iterations; ++iteration)
  {
    normal_equation_s current_equation{};
    const status_e evaluation_status =
      evaluate(current_pose, true, current_equation);
    if (evaluation_status != status_e::success) {
      return evaluation_status;
    }

    result.pose_map_from_scan = to_pose(current_pose);
    result.iteration_count = iteration + 1U;
    result.correspondence_count = current_equation.correspondence_count;
    if (current_equation.correspondence_count < minimum_correspondences) {
      return status_e::insufficient_correspondences;
    }

    const common::float64_t current_mean_cost =
      current_equation.cost /
      static_cast<common::float64_t>(current_equation.correspondence_count);
    result.mean_mahalanobis_cost = current_mean_cost;

    current_equation.hessian.diagonal().array() += config.get_hessian_damping();
    const Eigen::LDLT<matrix6_t> decomposition{current_equation.hessian};
    if (decomposition.info() != Eigen::Success) {
      return status_e::numerical_failure;
    }

    vector6_t increment = decomposition.solve(-current_equation.gradient);
    if (decomposition.info() != Eigen::Success || !increment.allFinite()) {
      return status_e::numerical_failure;
    }
    limit_increment(increment, config);

    const common::float64_t translation_step =
      increment.template head<3>().norm();
    const common::float64_t rotation_step =
      increment.template tail<3>().norm();
    if (translation_step <= config.get_translation_convergence_m() &&
      rotation_step <= config.get_rotation_convergence_rad())
    {
      result.converged = true;
      return status_e::success;
    }

    bool accepted = false;
    pose_state_s accepted_pose = current_pose;
    normal_equation_s accepted_equation{};
    common::float64_t step_scale = 1.0;
    for (common::uint32_t search_step = 0U;
      search_step < maximum_line_search_steps;
      ++search_step)
    {
      const pose_state_s candidate_pose =
        apply_increment(current_pose, increment * step_scale);
      normal_equation_s candidate_equation{};
      const status_e candidate_status =
        evaluate(candidate_pose, false, candidate_equation);
      if (candidate_status != status_e::success) {
        return candidate_status;
      }
      if (candidate_equation.correspondence_count >=
        minimum_correspondences)
      {
        const common::float64_t candidate_mean_cost =
          candidate_equation.cost /
          static_cast<common::float64_t>(
          candidate_equation.correspondence_count);
        if (std::isfinite(candidate_mean_cost) &&
          candidate_mean_cost <= current_mean_cost)
        {
          accepted = true;
          accepted_pose = candidate_pose;
          accepted_equation = candidate_equation;
          break;
        }
      }
      step_scale *= 0.5;
    }

    if (!accepted) {
      return status_e::step_rejected;
    }

    current_pose = accepted_pose;
    result.pose_map_from_scan = to_pose(current_pose);
    result.correspondence_count = accepted_equation.correspondence_count;
    result.mean_mahalanobis_cost =
      accepted_equation.cost /
      static_cast<common::float64_t>(accepted_equation.correspondence_count);

    if (translation_step * step_scale <= config.get_translation_convergence_m() &&
      rotation_step * step_scale <= config.get_rotation_convergence_rad())
    {
      result.converged = true;
      return status_e::success;
    }
  }

  normal_equation_s final_equation{};
  const status_e final_status = evaluate(current_pose, false, final_equation);
  if (final_status != status_e::success) {
    return final_status;
  }
  result.pose_map_from_scan = to_pose(current_pose);
  result.iteration_count = maximum_iterations;
  result.correspondence_count = final_equation.correspondence_count;
  if (final_equation.correspondence_count != 0U) {
    result.mean_mahalanobis_cost =
      final_equation.cost /
      static_cast<common::float64_t>(final_equation.correspondence_count);
  }
  return status_e::not_converged;
}

}  // namespace localization::ndt_solver
